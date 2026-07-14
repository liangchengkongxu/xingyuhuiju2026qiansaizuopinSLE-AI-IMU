#ifndef CLOUD_UPLOAD_SERVICE_H
#define CLOUD_UPLOAD_SERVICE_H

#include <QObject>
#include <QString>
#include <QList>

struct CloudMatchStroke {
    QString actionType;
    int score = 0;
    QString aiSuggestion;
    int ballSpeedKmh = 0;
    int powerTen = 0;
};

struct CloudDrillHit {
    QString skillName;
    int score = 0;
    QString aiSuggestion;
    int ballSpeedKmh = 0;
    int powerTen = 0;
};

class CloudUploadService : public QObject {
    Q_OBJECT
public:
    explicit CloudUploadService(QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }
    QString userPhone() const { return m_userPhone; }
    QString deviceId() const { return m_deviceId; }
    QString lastStatus() const { return m_lastStatus; }

    void reloadConfig();
    void uploadMatch(const QString &title, int durationMin, const QList<CloudMatchStroke> &strokes);
    void uploadDrillSession(const QList<CloudDrillHit> &hits);

signals:
    void uploadFinished(bool ok, const QString &detail);

private:
    void postJson(const QString &path, const QByteArray &body, const QString &label);
    static QString drillActionKey(const QString &skillName);

    bool m_enabled = false;
    QString m_baseUrl;
    QString m_deviceId;
    QString m_userPhone;
    QString m_lastStatus;
};

#endif
