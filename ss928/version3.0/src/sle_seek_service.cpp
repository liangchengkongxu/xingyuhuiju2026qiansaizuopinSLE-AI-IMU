#include "sle_seek_service.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QTimer>
#include <algorithm>

namespace {

constexpr auto kSeekLogPath = "/tmp/sle_seek_lines";
constexpr auto kSeekDebugPath = "/tmp/sle_seek_ui.log";
constexpr int kBridgeScanSecDefault = 6;
constexpr int kRadioPrepMarginMs = 5000;
constexpr int kEarlyFinishStableMs = 1200;
constexpr int kEarlyFinishMinElapsedMs = 6500;

QString resolveSeekBridgeScript()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/ws73/sle_seek_bridge.sh"),
        QStringLiteral("/opt/widget_ui/ws73/sle_seek_bridge.sh"),
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p))
            return p;
    }
    return candidates.first();
}

void seekUiLog(const QString &line)
{
    QFile f(QString::fromUtf8(kSeekDebugPath));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    f.write(QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz")).toUtf8());
    f.write(" ");
    f.write(line.toUtf8());
    f.write("\n");
}

int bridgeScanSec()
{
    const QByteArray env = qgetenv("SLE_SEEK_SCAN_SEC");
    bool ok = false;
    const int sec = env.trimmed().toInt(&ok);
    if (ok && sec >= 4 && sec <= 60)
        return sec;
    return kBridgeScanSecDefault;
}

QString macCompactUpper(const QString &mac)
{
    QString s = mac.trimmed().toUpper();
    s.remove(QLatin1Char(':'));
    s.remove(QLatin1Char('-'));
    return s;
}

bool macAllowedByWhitelist(const QString &macNorm)
{
    return macCompactUpper(macNorm).startsWith(QStringLiteral("CCAD"));
}

QString knownMacForBroadcastName(const QString &name)
{
    Q_UNUSED(name);
    return QString();
}

} // namespace

int sleAssignDeviceId(const QString &macNormalized)
{
    const QString mac = macNormalized.trimmed().toUpper();
    if (mac.isEmpty() || mac == QStringLiteral("00:00:00:00:00:00"))
        return 0;
    if (!macAllowedByWhitelist(mac))
        return 0;
    /* 设备号 = 星闪 MAC 最后一位十六进制数字（如 …:01→1，…:0A→10），不再自行递增编号 */
    const QString hex = macCompactUpper(mac);
    if (hex.isEmpty())
        return 0;
    bool ok = false;
    const int n = QString(hex.right(1)).toInt(&ok, 16);
    return (ok && n > 0) ? n : 0;
}

SleSeekService::SleSeekService(QObject *parent)
    : QObject(parent)
{
    m_poll = new QTimer(this);
    m_poll->setInterval(40);
    connect(m_poll, &QTimer::timeout, this, &SleSeekService::pollSeekLog);

    m_scanTimer = new QTimer(this);
    m_scanTimer->setSingleShot(true);
    connect(m_scanTimer, &QTimer::timeout, this, &SleSeekService::onScanTimer);

    m_earlyFinishTimer = new QTimer(this);
    m_earlyFinishTimer->setSingleShot(true);
    connect(m_earlyFinishTimer, &QTimer::timeout, this, [this]() {
        if (m_scanning && !m_devices.isEmpty())
            finishScan();
    });
}

void SleSeekService::prepRadio()
{
    if (m_scanning)
        return;
    const QString bridge = resolveSeekBridgeScript();
    if (!QFileInfo::exists(bridge))
        return;

    QFile stamp(QStringLiteral("/tmp/ws73_seek_prep.stamp"));
    if (stamp.open(QIODevice::ReadOnly)) {
        const qint64 ts = QString::fromUtf8(stamp.readAll()).trimmed().toLongLong();
        const qint64 age = QDateTime::currentSecsSinceEpoch() - ts;
        if (age >= 0 && age < 90) {
            seekUiLog(QStringLiteral("prepRadio skipped fresh stamp age=%1s").arg(age));
            return;
        }
    }

    seekUiLog(QStringLiteral("prepRadio startDetached"));
    QProcess::startDetached(QStringLiteral("/bin/sh"), {bridge, QStringLiteral("prep")});
}

QList<SleSeekDevice> SleSeekService::deviceList() const
{
    QList<SleSeekDevice> list;
    list.reserve(m_devices.size());
    for (auto it = m_devices.constBegin(); it != m_devices.constEnd(); ++it)
        list.append(it.value());
    std::sort(list.begin(), list.end(), [](const SleSeekDevice &a, const SleSeekDevice &b) {
        if (a.deviceId > 0 && b.deviceId <= 0)
            return true;
        if (b.deviceId > 0 && a.deviceId <= 0)
            return false;
        if (a.deviceId != b.deviceId)
            return a.deviceId < b.deviceId;
        return a.rssi > b.rssi;
    });
    return list;
}

void SleSeekService::scheduleEarlyFinish()
{
    if (!m_scanning || m_devices.isEmpty())
        return;
    const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - m_scanStartMs;
    if (elapsed < kEarlyFinishMinElapsedMs)
        return;
    const int count = m_devices.size();
    if (count == m_earlyFinishDeviceCount && m_earlyFinishTimer->isActive())
        return;
    m_earlyFinishDeviceCount = count;
    m_earlyFinishTimer->start(kEarlyFinishStableMs);
}

QString SleSeekService::normalizeMac(const QString &mac)
{
    QString s = mac.trimmed().toUpper();
    s.replace(QLatin1Char('-'), QLatin1Char(':'));
    return s;
}

int SleSeekService::mapDeviceId(const QString &macNorm)
{
    return sleAssignDeviceId(macNorm);
}

void SleSeekService::startScan(int durationMs)
{
    if (m_scanning)
        stopScan();

    const QString bridge = resolveSeekBridgeScript();
    if (!QFileInfo::exists(bridge)) {
        emit statusMessage(QStringLiteral("未找到星闪扫描脚本: %1").arg(bridge));
        emit scanFinished({});
        return;
    }

    m_devices.clear();
    m_logOffset = 0;
    m_scanning = true;
    m_scanStartMs = QDateTime::currentMSecsSinceEpoch();
    m_earlyFinishDeviceCount = 0;
    m_earlyFinishTimer->stop();

    const int scanSec = bridgeScanSec();
    const int waitMs = durationMs > 0 ?
        durationMs : (scanSec * 1000 + kRadioPrepMarginMs);

    seekUiLog(QStringLiteral("startScan waitMs=%1 bridge=%2").arg(waitMs).arg(bridge));

    /* 勿 blocking prep：WS73 完整复位约 8s，会卡死 UI 且与 bridge 内 prep 重复 */
    QProcess::execute(QStringLiteral("/bin/sh"), {bridge, QStringLiteral("stop")});
    QFile::remove(QString::fromUtf8(kSeekLogPath));
    if (!QProcess::startDetached(QStringLiteral("/bin/sh"), {bridge, QStringLiteral("start")})) {
        m_scanning = false;
        emit statusMessage(QStringLiteral("启动星闪扫描失败"));
        emit scanFinished({});
        seekUiLog(QStringLiteral("startDetached failed"));
        return;
    }

    m_poll->start();
    m_scanTimer->start(qMax(9000, waitMs));
    emit statusMessage(QStringLiteral("正在扫描星闪广播…（约 %1 秒）")
                           .arg(qMax(6, (waitMs + 999) / 1000)));
}

void SleSeekService::stopScan()
{
    if (!m_scanning)
        return;
    finishScan();
}

void SleSeekService::onScanTimer()
{
    finishScan();
}

void SleSeekService::finishScan()
{
    m_scanTimer->stop();
    m_earlyFinishTimer->stop();
    m_poll->stop();
    pollSeekLog();

    const QString bridge = resolveSeekBridgeScript();
    if (QFileInfo::exists(bridge))
        QProcess::execute(QStringLiteral("/bin/sh"), {bridge, QStringLiteral("stop")});

    m_scanning = false;

    const QList<SleSeekDevice> list = deviceList();
    seekUiLog(QStringLiteral("finishScan count=%1").arg(list.size()));

    emit statusMessage(QStringLiteral("扫描完成，发现 %1 台设备").arg(list.size()));
    emit scanFinished(list);
}

void SleSeekService::pollSeekLog()
{
    QFile f(QString::fromUtf8(kSeekLogPath));
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
        parseLine(QString::fromUtf8(raw));
    }
    m_logOffset = f.pos();
}

void SleSeekService::parseLine(const QString &line)
{
    QString s = line.trimmed();
    if (!s.startsWith(QStringLiteral("SLE_DEVICE|")))
        return;

    QString mac;
    QString name = QStringLiteral("-");
    int rssi = -127;
    int level = -1;
    int power = 0;
    int hasLevel = 0;
    int hasPower = 0;

    const QStringList parts = s.split(QLatin1Char('|'), Qt::KeepEmptyParts);
    for (const QString &part : parts) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = part.left(eq);
        const QString val = part.mid(eq + 1);
        if (key == QStringLiteral("mac"))
            mac = val;
        else if (key == QStringLiteral("name"))
            name = val;
        else if (key == QStringLiteral("rssi"))
            rssi = val.toInt();
        else if (key == QStringLiteral("level"))
            level = val.toInt();
        else if (key == QStringLiteral("power"))
            power = val.toInt();
        else if (key == QStringLiteral("has_level"))
            hasLevel = val.toInt();
        else if (key == QStringLiteral("has_power"))
            hasPower = val.toInt();
    }

    QString macNorm = normalizeMac(mac);
    if (macNorm.isEmpty() || macNorm == QStringLiteral("00:00:00:00:00:00")) {
        const QString fromName = knownMacForBroadcastName(name);
        if (fromName.isEmpty())
            return;
        macNorm = fromName;
    }
    if (!macAllowedByWhitelist(macNorm))
        return;

    bool isNew = false;
    SleSeekDevice dev;
    if (m_devices.contains(macNorm)) {
        dev = m_devices.value(macNorm);
        if (rssi < dev.rssi)
            return;
    } else {
        isNew = true;
    }

    dev.mac = macNorm;
    dev.name = name;
    dev.rssi = rssi;
    dev.discoveryLevel = level;
    dev.txPowerDbm = power;
    dev.hasDiscoveryLevel = (hasLevel != 0);
    dev.hasTxPower = (hasPower != 0);
    dev.deviceId = mapDeviceId(macNorm);
    m_devices.insert(macNorm, dev);
    emit devicesUpdated(deviceList());
    if (isNew)
        scheduleEarlyFinish();
}

#include "sle_seek_service.moc"
