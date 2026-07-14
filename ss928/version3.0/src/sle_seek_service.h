#ifndef SLE_SEEK_SERVICE_H
#define SLE_SEEK_SERVICE_H

#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QTimer>

struct SleSeekDevice {
    QString mac;
    QString name;
    int rssi = -127;
    int discoveryLevel = -1;
    int txPowerDbm = 0;
    bool hasDiscoveryLevel = false;
    bool hasTxPower = false;
    int deviceId = 0;
};

class SleSeekService : public QObject {
    Q_OBJECT
public:
    explicit SleSeekService(QObject *parent = nullptr);

    bool isScanning() const { return m_scanning; }

public slots:
    void startScan(int durationMs = 0);
    void stopScan();
    void prepRadio();

signals:
    void scanFinished(const QList<SleSeekDevice> &devices);
    void devicesUpdated(const QList<SleSeekDevice> &devices);
    void statusMessage(const QString &msg);

private slots:
    void pollSeekLog();
    void onScanTimer();

private:
    void parseLine(const QString &line);
    static QString normalizeMac(const QString &mac);
    static int mapDeviceId(const QString &macNorm);
    void finishScan();
    void scheduleEarlyFinish();
    QList<SleSeekDevice> deviceList() const;

    QTimer *m_poll = nullptr;
    QTimer *m_scanTimer = nullptr;
    QTimer *m_earlyFinishTimer = nullptr;
    int m_earlyFinishDeviceCount = 0;
    qint64 m_logOffset = 0;
    qint64 m_scanStartMs = 0;
    QMap<QString, SleSeekDevice> m_devices;
    bool m_scanning = false;
};

int sleAssignDeviceId(const QString &macNormalized);

#endif
