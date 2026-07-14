#ifndef SLE_IMU_SERVICE_H
#define SLE_IMU_SERVICE_H

#include <QHash>
#include <QObject>
#include <QString>

class QTimer;
class ImuSwingDetector;
class ImuCnnClassifier;

class SleImuService : public QObject {
    Q_OBJECT
public:
    explicit SleImuService(QObject *parent = nullptr);
    ~SleImuService() override;

    bool isActive() const { return m_active; }
    bool isConnected() const { return m_connected; }
    int packetCount() const { return m_packetCount; }
    bool isStrokeClassifierReady() const { return m_cnnReady; }

public slots:
    void start();
    void stop();
    void resetSwingDetector();

signals:
    void hitDetected(const QString &mac, double speedKmh, int powerTen, double peakDynG, double peakGyro,
        int durationMs, const QString &hitType, int strokeClassId, float strokeConfidence);
    void imuPacket(int tMs, double mG, double dynG);
    void statusMessage(const QString &msg);
    void connectionChanged(bool connected);

private slots:
    void pollImuLog();

private:
    ImuSwingDetector *detectorForMac(const QString &mac);
    void handleLine(const QString &line);
    QString resolveBridgeScript() const;
    QString resolveCnnModelPath() const;
    static QString normalizeMac(const QString &mac);

    QTimer *m_poll = nullptr;
    QHash<QString, ImuSwingDetector *> m_detectors;
    QHash<QString, int> m_lastSampleTByMac;
    ImuCnnClassifier *m_cnn = nullptr;
    qint64 m_logOffset = 0;
    bool m_active = false;
    bool m_wantRun = false;
    bool m_connected = false;
    bool m_cnnReady = false;
    int m_packetCount = 0;
};

#endif
