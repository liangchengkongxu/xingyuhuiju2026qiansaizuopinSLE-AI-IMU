#ifndef MAIN_WINDOW_H
#define MAIN_WINDOW_H

#include <QWidget>
#include <QStackedWidget>
#include <QFrame>
#include <QTimer>
#include <QStringList>
#include <QResizeEvent>

#include "ui_pages.h"
#include "sle_imu_service.h"
#include "sle_seek_service.h"
#include "widget_yolo_action.h"
#include "cloud_upload_service.h"

class MainWindow : public QWidget {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);

    void switchPage(int index);
    void goHome();
    void beginClassTrainFromMulti(const QStringList &deviceCodes, const QList<SleSeekDevice> &seekDevices);
    SleImuService *sleImu() const { return m_sleImu; }
    SleSeekService *sleSeek() const { return m_sleSeek; }
    WidgetYoloActionService *yoloAction() const { return m_yoloAction; }
    CloudUploadService *cloudUpload() const { return m_cloudUpload; }

    QStackedWidget *m_stack;
    QFrame *m_screenCard;
    HomePage *m_home;
    SinglePage *m_single;
    PracticePage *m_practice;
    SkillDetailPage *m_skillDetail;
    TrainingPage *m_training;
    TrainingSummaryPage *m_trainingSummary;
    ActionDetailPage *m_actionDetail;
    MatchPage *m_match;
    MatchSetupPage *m_matchSetup;
    MatchRunningPage *m_matchRunning;
    MatchReportPage *m_matchReport;
    MultiPage *m_multi;
    GroupPage *m_group;
    ClassTrainPage *m_classTrain;
    ClassTrainSummaryPage *m_classTrainSummary;
    SinglePracticeSetupPage *m_singlePracticeSetup;
    ClassHitDetailPage *m_classHitDetail;

    QString m_singlePracticeDeviceCode;
    QString m_singlePracticeDeviceName;
    QString m_singlePracticeDeviceMac;
    int m_singlePracticeDeviceId = 0;

    QList<MatchPlayerBinding> m_matchPlayers;

    void beginMatchAfterBind(const QList<MatchPlayerBinding> &players);
    void beginSinglePracticeAfterBind(int deviceId, const QString &deviceCode,
                                      const QString &deviceName, const QString &mac);
    void setStatus(const QString &) {}

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void updateScreenCardSize();
    void syncCameraVoOverlayFile();
    void uploadSoloDrillIfNeeded();
    QTimer *m_cameraVoTimer = nullptr;
    SleImuService *m_sleImu = nullptr;
    SleSeekService *m_sleSeek = nullptr;
    WidgetYoloActionService *m_yoloAction = nullptr;
    CloudUploadService *m_cloudUpload = nullptr;
};

#endif
