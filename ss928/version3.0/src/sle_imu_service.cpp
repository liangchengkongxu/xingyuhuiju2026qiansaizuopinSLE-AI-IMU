#include "sle_imu_service.h"

#include "imu_cnn_classifier.h"
#include "imu_swing_detector.h"

#include "badminton_npu_infer.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTextStream>
#include <QTimer>
#include <QtGlobal>

#include <signal.h>
#include <unistd.h>

namespace {

constexpr auto kImuLogPath = "/tmp/sle_imu_lines";
constexpr auto kDebugLogPath = "/tmp/widget_imu.log";

void appendDebugLog(const QString &line)
{
    QFile f(QString::fromUtf8(kDebugLogPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"))
       << QLatin1Char(' ') << line << QLatin1Char('\n');
}

bool isProcessRunning(const char *commName)
{
    QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool ok = false;
        if (entry.toInt(&ok) <= 0 || !ok)
            continue;
        QFile commFile(QStringLiteral("/proc/%1/comm").arg(entry));
        if (!commFile.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        if (commFile.readAll().trimmed() == commName)
            return true;
    }
    return false;
}

} // namespace

SleImuService::SleImuService(QObject *parent)
    : QObject(parent)
    , m_cnn(new ImuCnnClassifier())
{
    m_poll = new QTimer(this);
    m_poll->setInterval(50);
    connect(m_poll, &QTimer::timeout, this, &SleImuService::pollImuLog);
}

SleImuService::~SleImuService()
{
    stop();
    qDeleteAll(m_detectors);
    m_detectors.clear();
    delete m_cnn;
    m_cnn = nullptr;
}

QString SleImuService::normalizeMac(const QString &mac)
{
    QString s = mac.trimmed().toUpper();
    s.replace(QLatin1Char('-'), QLatin1Char(':'));
    return s;
}

ImuSwingDetector *SleImuService::detectorForMac(const QString &mac)
{
    QString key = normalizeMac(mac);
    if (key.isEmpty())
        key = QStringLiteral("_unknown_");

    ImuSwingDetector *det = m_detectors.value(key, nullptr);
    if (!det) {
        det = new ImuSwingDetector();
        det->reset();
        m_detectors.insert(key, det);
    }
    return det;
}

QString SleImuService::resolveBridgeScript() const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/ws73/sle_imu_bridge.sh"),
        QStringLiteral("/opt/widget_ui/ws73/sle_imu_bridge.sh"),
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p))
            return p;
    }
    return candidates.first();
}

QString SleImuService::resolveCnnModelPath() const
{
    const char *env = qgetenv("WIDGET_IMU_CNN_MODEL").constData();
    if (env != nullptr && env[0] != '\0') {
        return QString::fromUtf8(env);
    }
    const QStringList candidates = {
        QStringLiteral("/opt/widget_ui/models/badminton_npu.om"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/models/badminton_npu.om"),
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p))
            return p;
    }
    return candidates.first();
}

static QString resolveHitMode(bool cnnReady)
{
    const QByteArray hitSrc = qgetenv("WIDGET_HIT_SOURCE").trimmed().toLower();
    const bool imuOnly = (hitSrc == QByteArrayLiteral("imu") || hitSrc == QByteArrayLiteral("racket")
                          || hitSrc == QByteArrayLiteral("sle"));

    const QString mode = QString::fromUtf8(qgetenv("WIDGET_IMU_HIT_MODE")).trimmed().toLower();
    if (mode == QStringLiteral("rule")) {
        return QStringLiteral("rule");
    }
    if (mode == QStringLiteral("both")) {
        return QStringLiteral("both");
    }
    if (mode == QStringLiteral("cnn")) {
        return QStringLiteral("cnn");
    }
    /* 仅九轴：默认规则 FSM 触发（灵敏），CNN 只做动作分类 */
    if (imuOnly) {
        return QStringLiteral("rule");
    }
    return cnnReady ? QStringLiteral("cnn") : QStringLiteral("rule");
}

static double speedKmhFromPeak(double peakDynG, double peakGyroDps, double peakMG, int durationMs = -1)
{
    SwingMetrics m;
    m.peakDynG = peakDynG;
    m.peakGyroDps = peakGyroDps;
    m.peakMG = peakMG;
    m.durationMs = durationMs >= 0 ? durationMs : 0;
    return estimateSpeedKmh(m);
}

void SleImuService::resetSwingDetector()
{
    for (ImuSwingDetector *det : m_detectors)
        det->reset();
    m_lastSampleTByMac.clear();
    if (m_cnn != nullptr) {
        m_cnn->resetAll();
    }
}

void SleImuService::start()
{
    m_wantRun = true;

    /* 桥接已在跑时不要重复 start（页面切换会多次调用，否则会反复复位 WS73 / 打印 wifi_sta_stop） */
    if (m_active && m_poll && m_poll->isActive()) {
        QFile pidF(QStringLiteral("/tmp/sle_imu_bridge.pid"));
        if (pidF.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QByteArray pidBytes = pidF.readAll().trimmed();
            bool ok = false;
            const qint64 pid = pidBytes.toLongLong(&ok);
            if (ok && pid > 0 && kill(static_cast<pid_t>(pid), 0) == 0) {
                return;
            }
        }
    }

    m_active = true;

    const QString bridge = resolveBridgeScript();
    if (!QFileInfo::exists(bridge)) {
        m_active = false;
        emit statusMessage(QStringLiteral("未找到 IMU 桥接脚本: %1").arg(bridge));
        appendDebugLog(QStringLiteral("missing bridge %1").arg(bridge));
        return;
    }

    if (qEnvironmentVariableIsEmpty("WIDGET_IMU_CNN_DISABLE")) {
        bool skipInit = false;
        if (qEnvironmentVariableIntValue("WIDGET_IMU_CNN_SKIP_IF_VIO") != 0
            && isProcessRunning("sample_vio_ai")) {
            skipInit = true;
            appendDebugLog(QStringLiteral("imu cnn skipped: WIDGET_IMU_CNN_SKIP_IF_VIO=1 and sample_vio_ai running"));
        }
        if (!skipInit) {
            const QString om = resolveCnnModelPath();
            m_cnnReady = m_cnn->init(om);
            if (m_cnnReady) {
                appendDebugLog(QStringLiteral("imu cnn ready %1").arg(om));
            } else {
                appendDebugLog(QStringLiteral("imu cnn init failed %1").arg(om));
            }
        }
    } else {
        m_cnnReady = false;
    }

    QProcess::execute(QStringLiteral("/bin/sh"), {bridge, QStringLiteral("start")});

    const QFileInfo logInfo(QString::fromUtf8(kImuLogPath));
    if (logInfo.exists() && logInfo.size() > 0 && m_packetCount > 0) {
        m_logOffset = logInfo.size();
    } else {
        m_logOffset = 0;
    }

    if (!m_poll->isActive())
        m_poll->start();
    emit statusMessage(m_cnnReady
                           ? QStringLiteral("拍柄 IMU + 1D CNN 击球识别已就绪")
                           : QStringLiteral("正在扫描拍柄 IMU 广播…"));
    appendDebugLog(QStringLiteral("bridge ensure %1 cnn=%2").arg(bridge).arg(m_cnnReady ? 1 : 0));
}

void SleImuService::stop()
{
    m_wantRun = false;
    m_poll->stop();

    const QString bridge = resolveBridgeScript();
    if (QFileInfo::exists(bridge))
        QProcess::execute(QStringLiteral("/bin/sh"), {bridge, QStringLiteral("stop")});

    m_active = false;
    m_cnnReady = false;
    if (m_connected) {
        m_connected = false;
        emit connectionChanged(false);
    }
    appendDebugLog(QStringLiteral("bridge stop"));
}

void SleImuService::pollImuLog()
{
    QFile f(QString::fromUtf8(kImuLogPath));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    if (m_logOffset > f.size())
        m_logOffset = 0;
    if (!f.seek(m_logOffset))
        return;

    while (!f.atEnd()) {
        const QByteArray raw = f.readLine();
        if (raw.isEmpty())
            break;
        handleLine(QString::fromUtf8(raw));
    }
    m_logOffset = f.pos();
}

void SleImuService::handleLine(const QString &line)
{
    QString s = line.trimmed();
    if (s.isEmpty())
        return;

    if (s.startsWith(QStringLiteral("[bridge]"))) {
        emit statusMessage(s);
        return;
    }

    static const QString kPrefix = QStringLiteral("[SLE_IMU]");
    if (s.startsWith(kPrefix))
        s = s.mid(kPrefix.length()).trimmed();

    if (s.contains(QStringLiteral("扫描拍柄 IMU 广播"))) {
        emit statusMessage(QStringLiteral("正在接收拍柄 IMU 广播…"));
    }

    QString mac;
    ImuSample sample;
    if (!parseImuTaggedLine(s, &sample, &mac))
        return;

    if (sample.mG > 3.5 || sample.mG < 0.25) {
        appendDebugLog(QStringLiteral("drop insane M=%1 line=%2").arg(sample.mG).arg(s.left(80)));
        return;
    }

    QString macKey = normalizeMac(mac);
    if (macKey.isEmpty())
        macKey = QStringLiteral("_unknown_");
    if (m_lastSampleTByMac.value(macKey, -1) == sample.tMs)
        return;
    m_lastSampleTByMac.insert(macKey, sample.tMs);

    if (!m_connected) {
        m_connected = true;
        emit connectionChanged(true);
        emit statusMessage(QStringLiteral("已收到拍柄 IMU 广播"));
        appendDebugLog(QStringLiteral("first packet"));
    }

    m_packetCount++;
    const double dynG = qMax(0.0, sample.mG - 1.0);
    if (m_packetCount <= 3 || (m_packetCount % 100) == 0) {
        appendDebugLog(QStringLiteral("pkt #%1 mac=%2 t=%3 M=%4")
                          .arg(m_packetCount)
                          .arg(mac.isEmpty() ? QStringLiteral("-") : mac)
                          .arg(sample.tMs)
                          .arg(sample.mG, 0, 'f', 2));
    }

    emit imuPacket(sample.tMs, sample.mG, dynG);

    if (m_cnn != nullptr) {
        m_cnn->pushSample(mac, sample);
    }

    const QString hitMode = resolveHitMode(m_cnnReady);
    const bool useCnn = m_cnnReady && (hitMode == QStringLiteral("cnn") || hitMode == QStringLiteral("both"));
    const bool useRule = (hitMode == QStringLiteral("rule") || hitMode == QStringLiteral("both"));

    /* both 模式：CNN 峰值触发优先；未命中时规则 FSM 补漏并用 CNN 分类 */
    if (useCnn && m_cnn != nullptr) {
        const auto cnnHit = m_cnn->tryDetectHit(mac);
        if (cnnHit.has_value()) {
            const double peakDyn = qMax(0.0, cnnHit->peakMG - 1.0);
            const int dur = qMax(1, cnnHit->swingTEndMs - cnnHit->swingT0Ms);
            const double speed = speedKmhFromPeak(peakDyn, cnnHit->peakGyro, cnnHit->peakMG, dur);
            SwingMetrics powerMetrics;
            powerMetrics.peakDynG = peakDyn;
            powerMetrics.peakGyroDps = cnnHit->peakGyro;
            powerMetrics.peakMG = cnnHit->peakMG;
            powerMetrics.durationMs = dur;
            const int power = estimatePowerTen(powerMetrics);
            const int durEmit = dur;
            appendDebugLog(QStringLiteral("hit(cnn) mac=%1 type=%2 conf=%3 speed=%4 M=%5")
                               .arg(mac.isEmpty() ? QStringLiteral("-") : mac)
                               .arg(cnnHit->stroke.labelCn)
                               .arg(cnnHit->stroke.confidence, 0, 'f', 3)
                               .arg(speed, 0, 'f', 1)
                               .arg(cnnHit->peakMG, 0, 'f', 2));
            if (!qEnvironmentVariableIsEmpty("WIDGET_IMU_CNN_DEBUG")) {
                QString probLine;
                for (int i = 0; i < BADMINTON_NPU_NUM_CLASSES; ++i) {
                    if (i > 0) {
                        probLine += QLatin1Char(' ');
                    }
                    probLine += QString::fromUtf8(badminton_npu_label_cn(i))
                        + QLatin1Char('=')
                        + QString::number(cnnHit->probs[i], 'f', 2);
                }
                appendDebugLog(QStringLiteral("cnn_probs mac=%1 %2").arg(mac).arg(probLine));
            }
            emit hitDetected(mac, speed, power, peakDyn, cnnHit->peakGyro, durEmit, cnnHit->stroke.labelCn,
                cnnHit->stroke.classId, cnnHit->stroke.confidence);
            return;
        }
    }

    if (useRule) {
        ImuSwingDetector *det = detectorForMac(mac);
        const std::optional<SwingEstimate> est = det->feed(sample);
        if (est.has_value()) {
            const int ruleCooldownMs = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_COOLDOWN_MS");
            const int dedupMs = ruleCooldownMs > 0 ? ruleCooldownMs : 900;
            if (m_cnn != nullptr && m_cnn->hadRecentHit(mac, sample.tMs, dedupMs)) {
                return;
            }

            QString hitType = QStringLiteral("挥拍");
            int strokeClassId = -1;
            float strokeConf = 0.0f;
            if (m_cnnReady && m_cnn != nullptr) {
                auto stroke = m_cnn->classifySwing(mac, est->swingT0Ms, est->swingTEndMs);
                if (!stroke.has_value()) {
                    stroke = m_cnn->classifySwing(mac, est->swingT0Ms, est->swingTEndMs, 0.08f);
                }
                if (stroke.has_value()) {
                    hitType = stroke->labelCn;
                    strokeClassId = stroke->classId;
                    strokeConf = stroke->confidence;
                }
            }

            appendDebugLog(QStringLiteral("hit(rule) mac=%1 speed=%2 type=%3 conf=%4 M=%5 dyn=%6")
                               .arg(mac.isEmpty() ? QStringLiteral("-") : mac)
                               .arg(est->speedKmh)
                               .arg(hitType)
                               .arg(strokeConf, 0, 'f', 3)
                               .arg(est->peakMG, 0, 'f', 2)
                               .arg(est->peakDynG, 0, 'f', 3));
            emit hitDetected(mac, est->speedKmh, est->powerTen, est->peakDynG, est->peakGyro, est->durationMs,
                hitType, strokeClassId, strokeConf);
            return;
        }
    }
}

#include "sle_imu_service.moc"
