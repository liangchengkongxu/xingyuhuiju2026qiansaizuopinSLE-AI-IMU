#include "sle_measure_cmd.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>

namespace {

QString resolveAnnounceScript()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/ws73/sle_announce_cmd.sh"),
        QStringLiteral("/opt/widget_ui/ws73/sle_announce_cmd.sh"),
    };
    for (const QString &p : candidates) {
        if (QFileInfo::exists(p))
            return p;
    }
    return candidates.first();
}

bool runAnnounce(const char *verb, int deviceId, int durationMs)
{
    const QString script = resolveAnnounceScript();
    if (!QFileInfo::exists(script))
        return false;

    const int dev = qBound(0, deviceId, 254);
    const int dur = qMax(500, durationMs);
    const int code = QProcess::execute(
        QStringLiteral("/bin/sh"),
        {script, QString::fromUtf8(verb), QString::number(dev), QString::number(dur)});
    return code == 0;
}

} // namespace

namespace SleMeasureCmd {

bool sendStartMeasure(int deviceId, int durationMs)
{
    return runAnnounce("start", deviceId, durationMs);
}

bool sendStopMeasure(int deviceId, int durationMs)
{
    return runAnnounce("stop", deviceId, durationMs);
}

QString protocolSummary()
{
    return QStringLiteral(
        "Manufacturer 0xFF: EB 1A 01 CMD DEV SEQ_L SEQ_H; CMD 0xA1=start 0xA2=stop; DEV 0=all");
}

} // namespace SleMeasureCmd
