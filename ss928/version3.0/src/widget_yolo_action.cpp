#include "widget_yolo_action.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QMap>
#include <QRegularExpression>
#include <QTimer>
#include <QtMath>

namespace {

constexpr auto kActionPath = "/tmp/.widget_yolo_action";
constexpr auto kSwingEventsPath = "/tmp/.widget_yolo_swing_events";

struct LabelTable {
    QStringList en;
    QStringList cn;
    int count() const { return en.size(); }
};

LabelTable g_labels;

QString enToCn(const QString &en)
{
    static const QMap<QString, QString> kMap = {
        {QStringLiteral("Clear shot"), QStringLiteral("高远")},
        {QStringLiteral("Drive shot"), QStringLiteral("平抽")},
        {QStringLiteral("Drop-shot"), QStringLiteral("吊球")},
        {QStringLiteral("Drop shot"), QStringLiteral("放网")},
        {QStringLiteral("Lift Shot"), QStringLiteral("挑球")},
        {QStringLiteral("Lift shot"), QStringLiteral("挑球")},
        {QStringLiteral("Serve"), QStringLiteral("发球")},
        {QStringLiteral("Smash shot"), QStringLiteral("杀球")},
        {QStringLiteral("Net shot"), QStringLiteral("放网")},
        {QStringLiteral("Clear"), QStringLiteral("高远")},
        {QStringLiteral("Smash"), QStringLiteral("杀球")},
        {QStringLiteral("fangwang"), QStringLiteral("放网")},
        {QStringLiteral("gaoyuan"), QStringLiteral("高远")},
        {QStringLiteral("pingchou"), QStringLiteral("平抽")},
        {QStringLiteral("shaqiu"), QStringLiteral("杀球")},
        {QStringLiteral("tiaoqiu"), QStringLiteral("挑球")},
    };
    const QString key = en.trimmed();
    if (kMap.contains(key))
        return kMap.value(key);
    return key;
}

void loadDefaultLabels()
{
    g_labels.en = QStringList{
        QStringLiteral("fangwang"), QStringLiteral("gaoyuan"), QStringLiteral("pingchou"),
        QStringLiteral("shaqiu"), QStringLiteral("tiaoqiu")};
    g_labels.cn = QStringList{
        QStringLiteral("放网"), QStringLiteral("高远"), QStringLiteral("平抽"),
        QStringLiteral("杀球"), QStringLiteral("挑球")};
}

bool loadLabelMapFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QStringList en;
    QStringList cn;
    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 2)
            continue;
        bool ok = false;
        (void)parts[0].toInt(&ok);
        if (!ok)
            continue;
        const QString nameEn = parts.mid(1).join(QLatin1Char(' '));
        en.append(nameEn);
        cn.append(enToCn(nameEn));
    }
    if (en.isEmpty())
        return false;
    g_labels.en = en;
    g_labels.cn = cn;
    return true;
}

void ensureLabelsLoaded()
{
    static bool loaded = false;
    if (loaded)
        return;
    loaded = true;
    loadDefaultLabels();

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/models/label_map.txt"),
        QStringLiteral("/opt/widget_ui/models/label_map.txt"),
    };
    for (const QString &p : candidates) {
        if (loadLabelMapFile(p)) {
            qInfo() << "[WidgetYolo] label_map" << p << "nc=" << g_labels.count();
            return;
        }
    }
}

} // namespace

QString WidgetYoloActionService::classNameCn(int clsId)
{
    ensureLabelsLoaded();
    if (clsId >= 0 && clsId < g_labels.count())
        return g_labels.cn.at(clsId);
    return QStringLiteral("—");
}

QString WidgetYoloActionService::classNameEn(int clsId)
{
    ensureLabelsLoaded();
    if (clsId >= 0 && clsId < g_labels.count())
        return g_labels.en.at(clsId);
    return QStringLiteral("—");
}

WidgetYoloActionService::WidgetYoloActionService(QObject *parent)
    : QObject(parent)
{
    ensureLabelsLoaded();
    m_poll = new QTimer(this);
    m_poll->setInterval(50);
    connect(m_poll, &QTimer::timeout, this, &WidgetYoloActionService::pollActionFile);
}

void WidgetYoloActionService::start()
{
    m_poll->start();
}

void WidgetYoloActionService::stop()
{
    m_poll->stop();
}

void WidgetYoloActionService::resetSessionBaseline()
{
    m_sessionCount = 0;
    m_eventsOffset = 0;
    m_swingHistory.clear();

    QFile ef(QString::fromUtf8(kSwingEventsPath));
    if (ef.open(QIODevice::ReadOnly)) {
        m_eventsOffset = ef.size();
        ef.close();
    }

    QFile af(QString::fromUtf8(kActionPath));
    if (af.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const QString line = QString::fromUtf8(af.readLine()).trimmed();
        af.close();
        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() >= 10) {
            bool ok = false;
            const int seq = parts[9].toInt(&ok);
            if (ok && seq >= 0)
                m_swingSeq = seq;
        }
    }

    emit swingCountChanged(m_sessionCount);
}

void WidgetYoloActionService::appendSwingHistory(int seq, int clsId, float score, qint64 tsMs)
{
    WidgetSwingEvent ev;
    ev.seq = seq;
    ev.clsId = clsId;
    ev.score = score;
    ev.tsMs = tsMs > 0 ? tsMs : QDateTime::currentMSecsSinceEpoch();
    m_swingHistory.append(ev);
    trimSwingHistory();
}

void WidgetYoloActionService::trimSwingHistory()
{
    static constexpr int kMaxHistory = 512;
    static constexpr qint64 kMaxAgeMs = 120000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (m_swingHistory.size() > kMaxHistory)
        m_swingHistory.removeFirst();
    while (!m_swingHistory.isEmpty() && (now - m_swingHistory.first().tsMs) > kMaxAgeMs)
        m_swingHistory.removeFirst();
}

bool WidgetYoloActionService::bestSwingInWindow(qint64 centerMs, qint64 halfWindowMs, int &outClsId,
                                                float &outScore, QString &outNameCn) const
{
    const qint64 lo = centerMs - halfWindowMs;
    const qint64 hi = centerMs + halfWindowMs;
    bool found = false;
    float best = -1.0f;
    int bestCls = -1;
    for (const auto &ev : m_swingHistory) {
        if (ev.tsMs < lo || ev.tsMs > hi)
            continue;
        if (ev.clsId < 0)
            continue;
        if (!found || ev.score > best) {
            best = ev.score;
            bestCls = ev.clsId;
            found = true;
        }
    }
    if (!found)
        return false;
    outClsId = bestCls;
    outScore = best;
    outNameCn = classNameCn(bestCls);
    return true;
}

void WidgetYoloActionService::pollSwingEvents()
{
    ensureLabelsLoaded();
    const int maxCls = g_labels.count();

    QFile f(QString::fromUtf8(kSwingEventsPath));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    if (!f.seek(m_eventsOffset))
        return;

    bool any = false;
    while (!f.atEnd()) {
        const QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty())
            continue;

        const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (parts.size() < 3)
            continue;

        bool ok = false;
        const int seq = parts[0].toInt(&ok);
        const int cls = parts[1].toInt(&ok);
        const float swingScore = parts[2].toFloat(&ok);
        qint64 tsMs = QDateTime::currentMSecsSinceEpoch();
        if (parts.size() >= 4) {
            bool tsOk = false;
            const qint64 parsed = parts[3].toLongLong(&tsOk);
            if (tsOk && parsed > 0)
                tsMs = parsed;
        }
        if (!ok || seq <= 0 || cls < 0 || cls >= maxCls)
            continue;

        m_swingSeq = seq;
        m_sessionCount++;
        any = true;
        appendSwingHistory(seq, cls, swingScore, tsMs);
        const QString swingCn = classNameCn(cls);
        emit swingDetected(seq, cls, swingCn, swingScore);
    }

    m_eventsOffset = f.pos();
    f.close();

    if (any)
        emit swingCountChanged(m_sessionCount);
}

void WidgetYoloActionService::pollActionFile()
{
    ensureLabelsLoaded();
    const int maxCls = g_labels.count();

    pollSwingEvents();

    QFile f(QString::fromUtf8(kActionPath));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    const QString line = QString::fromUtf8(f.readLine()).trimmed();
    f.close();
    if (line.isEmpty())
        return;

    const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (parts.size() < 9)
        return;

    bool ok = false;
    (void)parts[0].toULongLong(&ok);
    const float score = parts[2].toFloat();
    const bool stable = parts[3].toInt() != 0;
    const int cls = parts[4].toInt(&ok);
    if (!ok)
        return;

    const QString nameEn = classNameEn(cls);
    const QString nameCn = classNameCn(cls);
    const bool hasDet = (cls >= 0 && cls < maxCls);
    const bool changed = (cls != m_clsId) || (stable != m_stable) ||
                         (qAbs(score - m_score) > 0.02f) || (nameCn != m_nameCn);

    m_clsId = cls;
    m_score = score;
    m_stable = stable;
    m_hasDetection = hasDet;
    m_nameEn = nameEn;
    m_nameCn = nameCn;

    if (stable && hasDet) {
        m_lastStableClsId = cls;
        m_lastStableNameCn = nameCn;
    }

    if (changed)
        emit actionUpdated(cls, nameCn, nameEn, score, stable);

    if (parts.size() >= 10) {
        bool okSeq = false;
        const int swingSeq = parts[9].toInt(&okSeq);
        if (okSeq && swingSeq >= 0)
            m_swingSeq = swingSeq;
    }
}

#include "widget_yolo_action.moc"
