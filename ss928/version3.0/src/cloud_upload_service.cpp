#include "cloud_upload_service.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QUrl>

namespace {

static void cloudLog(const QString &line)
{
    QFile f(QStringLiteral("/tmp/widget_cloud.log"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts << line << QLatin1Char('\n');
}

static QString trimVal(const QString &s)
{
    return s.trimmed();
}

} // namespace

CloudUploadService::CloudUploadService(QObject *parent)
    : QObject(parent)
{
    reloadConfig();
}

void CloudUploadService::reloadConfig()
{
    m_baseUrl = QStringLiteral("http://47.107.120.9/api/v1");
    m_deviceId = QStringLiteral("xingyu-ss928-01");
    m_userPhone = QStringLiteral("18340326998");
    m_enabled = false;

    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (env.contains(QStringLiteral("WIDGET_CLOUD_BASE_URL")))
        m_baseUrl = trimVal(env.value(QStringLiteral("WIDGET_CLOUD_BASE_URL")));
    if (env.contains(QStringLiteral("WIDGET_CLOUD_DEVICE_ID")))
        m_deviceId = trimVal(env.value(QStringLiteral("WIDGET_CLOUD_DEVICE_ID")));
    if (env.contains(QStringLiteral("WIDGET_CLOUD_USER_PHONE")))
        m_userPhone = trimVal(env.value(QStringLiteral("WIDGET_CLOUD_USER_PHONE")));
    if (env.contains(QStringLiteral("WIDGET_CLOUD_ENABLE")))
        m_enabled = trimVal(env.value(QStringLiteral("WIDGET_CLOUD_ENABLE"))) == QStringLiteral("1");

    const QStringList confPaths = {
        QStringLiteral("/opt/widget_ui/cloud.conf"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/cloud.conf"),
    };
    for (const QString &confPath : confPaths) {
        if (confPath.isEmpty())
            continue;
        QFile conf(confPath);
        if (!conf.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        while (!conf.atEnd()) {
            const QString line = QString::fromUtf8(conf.readLine()).trimmed();
            if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
                continue;
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QString key = line.left(eq).trimmed().toLower();
            const QString val = line.mid(eq + 1).trimmed();
            if (key == QStringLiteral("enabled"))
                m_enabled = (val == QStringLiteral("1") || val.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0);
            else if (key == QStringLiteral("base_url"))
                m_baseUrl = val;
            else if (key == QStringLiteral("device_id"))
                m_deviceId = val;
            else if (key == QStringLiteral("user_phone"))
                m_userPhone = val;
        }
        break;
    }

    if (env.contains(QStringLiteral("WIDGET_CLOUD_USER_PHONE")))
        m_userPhone = trimVal(env.value(QStringLiteral("WIDGET_CLOUD_USER_PHONE")));
    if (env.contains(QStringLiteral("WIDGET_CLOUD_DEVICE_ID")))
        m_deviceId = trimVal(env.value(QStringLiteral("WIDGET_CLOUD_DEVICE_ID")));
    if (env.contains(QStringLiteral("WIDGET_CLOUD_BASE_URL")))
        m_baseUrl = trimVal(env.value(QStringLiteral("WIDGET_CLOUD_BASE_URL")));
    if (env.contains(QStringLiteral("WIDGET_CLOUD_ENABLE")))
        m_enabled = trimVal(env.value(QStringLiteral("WIDGET_CLOUD_ENABLE"))) == QStringLiteral("1");

    while (m_baseUrl.endsWith(QLatin1Char('/')))
        m_baseUrl.chop(1);

    cloudLog(QStringLiteral("[config] enabled=%1 base=%2 device=%3 phone=%4")
                 .arg(m_enabled ? 1 : 0)
                 .arg(m_baseUrl, m_deviceId, m_userPhone));
}

QString CloudUploadService::drillActionKey(const QString &skillName)
{
    const QString s = skillName.trimmed();
    if (s.contains(QStringLiteral("放网")))
        return QStringLiteral("net_drop");
    if (s.contains(QStringLiteral("杀")))
        return QStringLiteral("smash");
    if (s.contains(QStringLiteral("高远")))
        return QStringLiteral("clear");
    if (s.contains(QStringLiteral("挑")))
        return QStringLiteral("lift");
    if (s.contains(QStringLiteral("平抽")))
        return QStringLiteral("drive");
    return QStringLiteral("smash");
}

void CloudUploadService::postJson(const QString &path, const QByteArray &body, const QString &label)
{
    if (!m_enabled) {
        m_lastStatus = QStringLiteral("云端上报已关闭");
        emit uploadFinished(false, m_lastStatus);
        return;
    }
    if (m_userPhone.trimmed().isEmpty()) {
        m_lastStatus = QStringLiteral("未配置 user_phone");
        cloudLog(QStringLiteral("[skip] %1: no user_phone").arg(label));
        emit uploadFinished(false, m_lastStatus);
        return;
    }

    const QUrl url(m_baseUrl + path);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setTransferTimeout(15000);

    auto *nam = new QNetworkAccessManager(this);
    QNetworkReply *reply = nam->post(req, body);
    cloudLog(QStringLiteral("[post] %1 %2 bytes -> %3").arg(label).arg(body.size()).arg(url.toString()));

    connect(reply, &QNetworkReply::finished, this, [this, reply, nam, label]() {
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray resp = reply->readAll();
        const bool ok = (reply->error() == QNetworkReply::NoError && http >= 200 && http < 300);
        m_lastStatus = ok ? QString::fromUtf8(resp) : QStringLiteral("%1 %2").arg(http).arg(QString::fromUtf8(resp));
        cloudLog(QStringLiteral("[%1] http=%2 err=%3 resp=%4")
                     .arg(ok ? QStringLiteral("ok") : QStringLiteral("fail"))
                     .arg(http)
                     .arg(reply->errorString())
                     .arg(QString::fromUtf8(resp)));
        emit uploadFinished(ok, m_lastStatus);
        reply->deleteLater();
        nam->deleteLater();
    });
}

void CloudUploadService::uploadMatch(const QString &title, int durationMin, const QList<CloudMatchStroke> &strokes)
{
    QJsonObject root;
    root.insert(QStringLiteral("device_id"), m_deviceId);
    root.insert(QStringLiteral("user_phone"), m_userPhone);
    root.insert(QStringLiteral("title"), title.isEmpty() ? QStringLiteral("板端对打") : title);
    root.insert(QStringLiteral("opponent_label"), QStringLiteral("对打伙伴"));
    if (durationMin > 0)
        root.insert(QStringLiteral("duration_min"), durationMin);

    QJsonArray arr;
    for (const CloudMatchStroke &st : strokes) {
        QJsonObject item;
        item.insert(QStringLiteral("action_type"), st.actionType.isEmpty() ? QStringLiteral("挥拍") : st.actionType);
        item.insert(QStringLiteral("score"), st.score);
        if (!st.aiSuggestion.isEmpty())
            item.insert(QStringLiteral("ai_suggestion"), st.aiSuggestion);
        if (st.ballSpeedKmh > 0)
            item.insert(QStringLiteral("ball_speed_kmh"), st.ballSpeedKmh);
        if (st.powerTen > 0)
            item.insert(QStringLiteral("power_n"), st.powerTen * 10);
        arr.append(item);
    }
    root.insert(QStringLiteral("strokes"), arr);

    postJson(QStringLiteral("/device/ingest/match"),
             QJsonDocument(root).toJson(QJsonDocument::Compact),
             QStringLiteral("match"));
}

void CloudUploadService::uploadDrillSession(const QList<CloudDrillHit> &hits)
{
    for (const CloudDrillHit &hit : hits) {
        QJsonObject root;
        root.insert(QStringLiteral("device_id"), m_deviceId);
        root.insert(QStringLiteral("user_phone"), m_userPhone);
        root.insert(QStringLiteral("action_type"), drillActionKey(hit.skillName));
        root.insert(QStringLiteral("score"), hit.score);
        if (!hit.aiSuggestion.isEmpty())
            root.insert(QStringLiteral("ai_suggestion"), hit.aiSuggestion);
        if (hit.ballSpeedKmh > 0)
            root.insert(QStringLiteral("ball_speed_kmh"), hit.ballSpeedKmh);
        if (hit.powerTen > 0)
            root.insert(QStringLiteral("power_n"), hit.powerTen * 10);
        postJson(QStringLiteral("/device/ingest/drill"),
                 QJsonDocument(root).toJson(QJsonDocument::Compact),
                 QStringLiteral("drill"));
    }
}

#include "cloud_upload_service.moc"
