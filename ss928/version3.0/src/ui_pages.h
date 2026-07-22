#ifndef UI_PAGES_H
#define UI_PAGES_H

#include <QWidget>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QScrollArea>
#include <QTimer>
#include <QMediaPlayer>
#include <QMediaPlaylist>
#include <QVideoWidget>
#include <QList>
#include <QMap>
#include <QVariant>
#include <QHideEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QDir>
#include <QFile>
#include <QIODevice>
#include <QImage>
#include <QPixmap>
#include <QStringList>
#include <QSet>

#include "ui_common.h"
#include "sle_imu_service.h"
#include "sle_seek_service.h"
#include "imu_swing_detector.h"
#include "class_hit_ai_advice.h"
#include "widget_yolo_action.h"

class MainWindow;

enum class PageHeaderMode { Normal, SingleCentered };

class SidebarWidget : public QWidget {
    Q_OBJECT
public:
    SidebarWidget(const QStringList &items, const QString &connectTitle,
                  QLineEdit *&codeInput, QLabel *&msgLabel, QPushButton *&connectBtn,
                  QWidget *parent = nullptr);
};

class PageBase : public QWidget {
    Q_OBJECT
public:
    PageBase(const QString &title, const QString &subtitle, MainWindow *mw, QWidget *parent = nullptr,
             PageHeaderMode headerMode = PageHeaderMode::Normal);
    void setBackTarget(const QString &backLabel, const QString &backPage = QString());

    QVBoxLayout *m_rootLayout;
    QLabel *m_titleLabel;
    QLabel *m_subtitleLabel;
    QPushButton *m_backBtn;
    QPushButton *m_homeBtn;
    MainWindow *m_mainWindow;
    QString currentSkill;
signals:
    void skillSelected(const QString &skill);
};

class SkillDetailPage : public PageBase {
    Q_OBJECT
public:
    SkillDetailPage(MainWindow *mw, QWidget *parent = nullptr);
    void setSkillName(const QString &name);
signals:
    void startPractice();

protected:
    void hideEvent(QHideEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void playTutorialForSkill(const QString &name);
    void refreshTipsContent(const QString &name);
    void highlightTutorialSpeedButton(QPushButton *active, double rate);
    void applyTutorialPlaybackRate();
    QString m_currentSkillName;
    QVBoxLayout *m_tipsContentLay = nullptr;
    QStackedWidget *m_videoStack = nullptr;
    QWidget *m_tutorialVideoPage = nullptr;
    QVideoWidget *m_tutorialVideo = nullptr;
    QLabel *m_tutorialPlaceholder = nullptr;
    QMediaPlayer *m_tutorialPlayer = nullptr;
    QMediaPlaylist *m_tutorialPlaylist = nullptr;
    QList<QPushButton *> m_tutorialSpeedButtons;
    double m_tutorialPlaybackRate = 1.0;
};

// ═══════════════════════════════════════════════
// 首页 (pageHome)
// ═══════════════════════════════════════════════
class HomePage : public QWidget {
    Q_OBJECT
public:
    HomePage(MainWindow *mw, QWidget *parent = nullptr);
};

// ═══════════════════════════════════════════════
// 单人模式 (pageSingle)
// ═══════════════════════════════════════════════
class SinglePage : public PageBase {
    Q_OBJECT
public:
    SinglePage(MainWindow *mw, QWidget *parent = nullptr);
};

// ═══════════════════════════════════════════════
// 练习模式 (pagePractice)
// ═══════════════════════════════════════════════
class PracticePage : public PageBase {
    Q_OBJECT
public:
    PracticePage(MainWindow *mw, QWidget *parent = nullptr);
signals:
    void skillSelected(const QString &skill);
};

// ═══════════════════════════════════════════════
// 训练中 (pageTraining)
// ═══════════════════════════════════════════════
class TrainingPage : public PageBase {
    Q_OBJECT
public:
    TrainingPage(MainWindow *mw, QWidget *parent = nullptr);
    void startTraining(const QString &skill, int practiceIndex);
    void startClassStudentTraining(const QString &studentName, const QString &deviceCode);
    void stopTraining();
    void resumeImu();
    void enableCameraHitListen(bool on);
    bool isClassStudentMode() const { return m_classStudentMode; }
    bool m_imuSubscribed = false;
    QWidget *cameraOverlayHost() const { return m_cameraOverlayHost; }
    QString m_classDeviceCode;
    QLabel *m_aiScoreValue;
    QLabel *m_aiScoreHint;
    QLabel *m_correctionText;
    QLabel *m_speedLabel = nullptr;
    QLabel *m_powerLabel = nullptr;
    QLabel *m_hitCountLabel = nullptr;
    QLabel *m_avgSpeedLabel = nullptr;
    QLabel *m_actionTypeLabel = nullptr;
    QString m_currentSkill;
    int m_practiceIndex;
    QList<int> m_scores;
    QList<int> m_speedsKmh;
    QList<int> m_powersTen;
    QList<QString> m_hitTypes;
    QList<int> m_durationsMs;
    QString m_replaySessionId;
    int speedAt(int oneBasedIdx) const;
    int powerAt(int oneBasedIdx) const;
    QString hitTypeAt(int oneBasedIdx) const;
    int durationAt(int oneBasedIdx) const;
    QString replaySessionId() const { return m_replaySessionId; }
    QString replayPathAt(int oneBasedIdx) const;
signals:
    void viewSummary();
    void exitTraining();

public slots:
    void onImuHitDetected(const QString &mac, double speedKmh, int powerTen, double peakDynG, double peakGyro,
        int durationMs, const QString &hitType, int strokeClassId, float strokeConfidence);
    void onYoloActionUpdated(int clsId, const QString &nameCn, const QString &nameEn, float score, bool stable);
    void onCameraSwingDetected(int swingSeq, int clsId, const QString &nameCn, float score);
    void onSwingCountChanged(int sessionCount);

private:
    void onImuHit(double speedKmh, int powerTen, int presetScore, const QString &hitType, int durationMs,
                  const QString &sourceHint = QString());
    void onCameraHit(int clsId, const QString &nameCn, float score, const QString &detail = QString());
    void applyHitAdvice(int hitIdx, const QString &hitType, int score, int speedKmh, int powerTen, int durationMs);
    bool tryAcceptPracticeHit(const QString &source);
    void refreshCameraSwingCountHint(int sessionCount, const QString &lastHitName);
    void subscribeImu();
    void unsubscribeImu();
    void applyTrainLayout();
    void applyTrainMetricsStyle(bool enlarged);
    void updateActionTypeLabel(int clsId, const QString &nameCn, float score, bool stable);
    void refreshFixedSkillTypeLabel();
    void registerHitReplay(int hitIdx);
    QFrame *m_cameraOverlayHost = nullptr;
    QFrame *m_cameraPanel = nullptr;
    QFrame *m_corrPanel = nullptr;
    QFrame *m_scorePanel = nullptr;
    QFrame *m_kpiCard = nullptr;
    QLabel *m_aiScoreTitle = nullptr;
    QLabel *m_scoreUnit = nullptr;
    QList<QFrame *> m_metricChips;
    QWidget *m_cameraCell = nullptr;
    QWidget *m_leftCol = nullptr;
    QGridLayout *m_trainGrid = nullptr;
    bool m_classStudentMode = false;
    bool m_cameraHitSubscribed = false;
    QString m_classStudentName;
    int m_classDeviceId = 0;
    int m_speedSum = 0;
    int m_speedCount = 0;
    int m_powerSum = 0;
    int m_powerCount = 0;
    QString m_lastSwingHitName;
    qint64 m_lastPracticeHitMs = 0;
    bool m_prevYoloStable = false;
    int m_prevYoloCls = -1;
    float m_prevYoloScore = 0.0f;
};

// ═══════════════════════════════════════════════
// 训练总结 (pageTrainingSummary)
// ═══════════════════════════════════════════════
class TrainingSummaryPage : public PageBase {
    Q_OBJECT
public:
    TrainingSummaryPage(MainWindow *mw, QWidget *parent = nullptr);
    void showSummary(const QString &skill, int practiceIndex, const QList<int> &scores,
                     const QList<int> &speedsKmh = QList<int>(),
                     const QList<int> &powersTen = QList<int>());
    void showClassStudentSummary(const QString &studentName, const QString &deviceCode,
                                  int swings, const QList<int> &scores,
                                  const QList<int> &speedsKmh = QList<int>(),
                                  const QList<int> &powersTen = QList<int>(),
                                  const QList<QString> &hitTypes = QList<QString>(),
                                  const QList<int> &durationsMs = QList<int>());
    QLabel *m_summaryTitle;
    QLabel *m_summarySub;
    QLabel *m_avgBadge;
    QGridLayout *m_scoreGrid;
    QString m_source; // "training" or "classTrain"
    QString m_classStudentName;
signals:
    void actionClicked(int idx, int score);
    void classHitClicked(int idx, int score, const QString &hitType, int speedKmh, int powerTen, int durationMs);
};

// ═══════════════════════════════════════════════
// 班级单次动作详情 (pageClassHitDetail) — 独立页面，无视频
// ═══════════════════════════════════════════════
class ClassHitDetailPage : public PageBase {
    Q_OBJECT
public:
    ClassHitDetailPage(MainWindow *mw, QWidget *parent = nullptr);
    void showHit(int idx, const QString &studentName, const QString &hitType, int score, int speedKmh,
                 int powerTen, int durationMs = -1);

private:
    QLabel *m_indexLabel = nullptr;
    QLabel *m_hitTypeValue = nullptr;
    QLabel *m_scoreValue = nullptr;
    QLabel *m_speedValue = nullptr;
    QLabel *m_powerValue = nullptr;
    QLabel *m_durationValue = nullptr;
    QLabel *m_hintText = nullptr;
};

// ═══════════════════════════════════════════════
// 帧序列回放（sample_vio_ai 导出 PPM；板端无 ffmpeg 时备用）
// ═══════════════════════════════════════════════
class FrameReplayWidget : public QWidget {
    Q_OBJECT
public:
    explicit FrameReplayWidget(QWidget *parent = nullptr) : QWidget(parent)
    {
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
        m_label = new QLabel(this);
        m_label->setAlignment(Qt::AlignCenter);
        m_label->setStyleSheet(QStringLiteral("background:#000;color:#8cc7ff;font-size:16px;"));
        m_label->setMinimumSize(kCamTrainingW, kCamTrainingH);
        lay->addWidget(m_label, 1);
        connect(&m_timer, &QTimer::timeout, this, &FrameReplayWidget::showNextFrame);
    }

    void loadFromDir(const QString &dirPath)
    {
        stopPlayback();
        m_dirPath = dirPath;
        m_frames.clear();
        const QDir dir(dirPath);
        m_frames = dir.entryList(QStringList() << QStringLiteral("frame_*.ppm"), QDir::Files, QDir::Name);
        m_index = 0;
        m_baseFps = 10;
        {
            QFile meta(dirPath + QStringLiteral("/meta.txt"));
            if (meta.open(QIODevice::ReadOnly)) {
                const QStringList lines = QString::fromUtf8(meta.readAll()).split(QLatin1Char('\n'));
                for (const QString &line : lines) {
                    if (line.startsWith(QStringLiteral("fps="))) {
                        bool ok = false;
                        const int fps = line.mid(4).trimmed().toInt(&ok);
                        if (ok && fps >= 4 && fps <= 30)
                            m_baseFps = fps;
                    }
                }
            }
        }
        if (m_frames.isEmpty()) {
            m_label->setText(QStringLiteral("回放帧加载失败"));
            m_label->setPixmap(QPixmap());
            return;
        }
        showCurrentFrame();
    }

    void setPlaybackRate(double rate)
    {
        m_rate = qMax(0.25, rate);
        updateTimerInterval();
    }

    void play()
    {
        if (m_frames.isEmpty())
            return;
        updateTimerInterval();
        m_timer.start();
    }

    void stopPlayback()
    {
        m_timer.stop();
        m_index = 0;
    }

private slots:
    void showNextFrame()
    {
        if (m_frames.isEmpty())
            return;
        m_index = (m_index + 1) % m_frames.size();
        showCurrentFrame();
    }

private:
    void showCurrentFrame()
    {
        if (m_frames.isEmpty())
            return;
        const QString path = m_dirPath + QLatin1Char('/') + m_frames.at(m_index);
        QImage img(path);
        if (img.isNull()) {
            m_label->setText(QStringLiteral("无法读取回放帧"));
            return;
        }
        m_label->setPixmap(QPixmap::fromImage(img.scaled(m_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    }

    void updateTimerInterval()
    {
        const int ms = qMax(1, static_cast<int>(1000.0 / (m_baseFps * m_rate)));
        m_timer.setInterval(ms);
    }

    QString m_dirPath;
    QLabel *m_label = nullptr;
    QTimer m_timer;
    QStringList m_frames;
    int m_index = 0;
    int m_baseFps = 15;
    double m_rate = 1.0;
};

// ═══════════════════════════════════════════════
// 动作详情 (pageActionDetail)
// ═══════════════════════════════════════════════
class ActionDetailPage : public PageBase {
    Q_OBJECT
public:
    ActionDetailPage(MainWindow *mw, QWidget *parent = nullptr);
    void showAction(int idx, int score, const QString &skillLabel, int speedKmh = -1, int powerTen = -1,
                    const QString &replaySessionId = QString(), int durationMs = -1,
                    const QString &hitType = QString(), int backPage = 5);
    QLabel *m_indexText;
    QLabel *m_scoreValue;
    QLabel *m_metricSpeed;
    QLabel *m_metricPower;
    QLabel *m_metricTiming;
    QLabel *m_metricSwing;
    QLabel *m_aiComment;
    QLabel *m_aiImprove;
    QString m_source; // "training" / "match" / "classTrain"

protected:
    void hideEvent(QHideEvent *event) override;

private slots:
    void tryStartReplay();
    void onReplayPlayerError(QMediaPlayer::Error);

private:
    void highlightSpeedButton(QPushButton *active, double rate);
    void stopReplay();
    void playReplayClip(const QString &clipPath);
    void applyPlaybackRate();
    QLabel *m_speedValueText = nullptr;
    QList<QPushButton *> m_speedButtons;
    QStackedWidget *m_videoStack = nullptr;
    QWidget *m_replayVideoPage = nullptr;
    QLabel *m_replayPlaceholder = nullptr;
    QVideoWidget *m_replayVideo = nullptr;
    QMediaPlayer *m_replayPlayer = nullptr;
    FrameReplayWidget *m_frameReplay = nullptr;
    QTimer *m_replayWaitTimer = nullptr;
    int m_replayWaitTicks = 0;
    QString m_pendingReplaySession;
    int m_pendingHitIdx = 0;
    int m_detailBackPage = 5;
    double m_playbackRate = 1.0;
};

// ═══════════════════════════════════════════════
// 对打/比赛模式 (pageMatch)
// ═══════════════════════════════════════════════
struct MatchPlayerBinding {
    int deviceId = 0;
    QString deviceCode;
    QString deviceName;
    QString mac;
};

class MatchPage : public PageBase {
    Q_OBJECT
public:
    MatchPage(MainWindow *mw, QWidget *parent = nullptr);
    void setConnectedPlayers(const QList<MatchPlayerBinding> &players);
    QWidget *cameraOverlayHost() const { return m_cameraOverlayHost; }
signals:
    void startMatch();

private:
    QFrame *m_cameraOverlayHost = nullptr;
    QFrame *m_cameraPanel = nullptr;
    QLabel *m_deviceStatusLabel = nullptr;
    QPushButton *m_connectBtn = nullptr;
    QPushButton *m_startBtn = nullptr;
};

// ═══════════════════════════════════════════════
// 对打模式：扫描拍柄（最多 4 台）(pageMatchSetup)
// ═══════════════════════════════════════════════
class MatchSetupPage : public PageBase {
    Q_OBJECT
public:
    MatchSetupPage(MainWindow *mw, QWidget *parent = nullptr);
    void resetScan();
public slots:
    void applyScanResults(const QList<SleSeekDevice> &devices);
    void onSeekStatus(const QString &msg);
    void afterScanUiReady();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private slots:
    void beginScan();
    void goConfirmStep();
    void confirmAndEnterMatch();
private:
    void showScanStep();
    void showConfirmStep();
    int computeDeviceGridCols() const;
    int computeDeviceCellWidth() const;
    void rebuildDeviceGrid();
    void rebuildConfirmList();
    void updateScanNextButton();
    QStackedWidget *m_stepStack = nullptr;
    QWidget *m_scanStep = nullptr;
    QWidget *m_confirmStep = nullptr;
    QPushButton *m_scanBtn = nullptr;
    QPushButton *m_scanNextBtn = nullptr;
    QPushButton *m_confirmBtn = nullptr;
    QLabel *m_scanMsg = nullptr;
    QLabel *m_scanCount = nullptr;
    QLabel *m_confirmHint = nullptr;
    QScrollArea *m_deviceScroll = nullptr;
    QWidget *m_deviceGrid = nullptr;
    QScrollArea *m_confirmScroll = nullptr;
    QWidget *m_confirmList = nullptr;
    int m_deviceGridCols = 0;
    QSet<int> m_selectedIndices;
    QList<SleSeekDevice> m_seekDevices;
signals:
    void playersConfirmed(const QList<MatchPlayerBinding> &players);
};

// ═══════════════════════════════════════════════
// 比赛中 (pageMatchRunning)
// ═══════════════════════════════════════════════
class MatchRunningPage : public PageBase {
    Q_OBJECT
public:
    MatchRunningPage(MainWindow *mw, QWidget *parent = nullptr);
    QWidget *cameraOverlayHost() const { return m_cameraOverlayHost; }
    void initPlayers(const QList<MatchPlayerBinding> &players);
    void startRunning();
    void stopRunning();
    void enableCameraHitListen(bool on);
    struct PlayerReportLine {
        QString name;
        int hits = 0;
        int avgScore = 0;
        int avgSpeed = 0;
    };
    struct MatchHitRecord {
        QString playerName;
        QString actionType;
        int score = 0;
        QString aiSuggestion;
        int speedKmh = 0;
        int powerTen = 0;
        int hitIdx = 0;       /* 全场击球序号，对应回放 hit_N */
        int durationMs = -1;
    };
    QList<PlayerReportLine> playerReportLines() const;
    QList<MatchHitRecord> hitRecords() const { return m_hitRecords; }
    QString replaySessionId() const { return m_replaySessionId; }
    int m_hits, m_speedSum, m_speedCount, m_maxSpeed;
    int m_powerSum, m_powerCount;
    qint64 m_startedAt, m_endedAt;
    QList<MatchPlayerBinding> m_players;

public slots:
    void onImuHitDetected(const QString &mac, double speedKmh, int powerTen, double peakDynG, double peakGyro,
        int durationMs, const QString &hitType, int strokeClassId, float strokeConfidence);
    void onYoloActionUpdated(int clsId, const QString &nameCn, const QString &nameEn, float score, bool stable);
    void onCameraSwingDetected(int swingSeq, int clsId, const QString &nameCn, float score);

private:
    struct PlayerStats {
        MatchPlayerBinding binding;
        QWidget *panel = nullptr;
        QLabel *nameLabel = nullptr;
        QLabel *aiScoreValue = nullptr;
        QLabel *hitCountLabel = nullptr;
        QLabel *speedLabel = nullptr;
        QLabel *powerLabel = nullptr;
        QLabel *avgSpeedLabel = nullptr;
        QLabel *maxSpeedLabel = nullptr;
        QLabel *actionTypeLabel = nullptr;
        QLabel *adviceLabel = nullptr;
        QLabel *statusHint = nullptr;
        int hits = 0;
        int speedSum = 0;
        int speedCount = 0;
        int maxSpeed = 0;
        int powerSum = 0;
        int powerCount = 0;
        QList<int> scores;
        qint64 lastHitMs = 0;
        qint64 lastImuSignalMs = 0;
    };

    struct PendingVisionHit {
        int playerIdx = -1;
        int hitIdx = 0;
        int replayHitIdx = 0;
        qint64 triggerMs = 0;
        qint64 resolveAfterMs = 0;
        int speedKmh = 0;
        int powerTen = 0;
        int durationMs = -1;
        int imuClsId = -1;
        float imuConf = 0.0f;
        QString imuType;
        int provisionalScore = 0;
        bool resolved = false;
    };

    void rebuildMatchUi();
    enum class MatchPanelStyle { TrainingLike, CompactDual, CompactQuad };
    QWidget *buildPlayerPanel(PlayerStats &ps, const QString &sideTitle, MatchPanelStyle style, bool showAdvice = true);
    void clearMatchBody();
    int playerIndexForMac(const QString &mac) const;
    int resolvePlayerForCameraHit() const;
    bool tryAcceptPlayerHit(PlayerStats &ps);
    void onPlayerImuHit(int playerIdx, double speedKmh, int powerTen, int durationMs, const QString &imuType,
                        int strokeClassId, float strokeConf);
    void onPlayerCameraHit(int playerIdx, int clsId, const QString &nameCn, float camScore);
    void scheduleVisionResolve(int playerIdx, int hitIdx, int replayHitIdx, qint64 triggerMs, int speedKmh,
                               int powerTen, int durationMs, const QString &imuType, int strokeClassId,
                               float strokeConf, int provisionalScore);
    void pollPendingVisionHits();
    void finalizeHitVision(PendingVisionHit &pending);
    void refreshPlayerPanel(int playerIdx);
    void registerHitReplay(int hitIdx);
    void subscribeImu();
    void unsubscribeImu();

    QGridLayout *m_matchGrid = nullptr;
    QWidget *m_bodyHost = nullptr;
    QWidget *m_leftCol = nullptr;
    QWidget *m_rightCol = nullptr;
    QWidget *m_centerCol = nullptr;
    QFrame *m_cameraOverlayHost = nullptr;
    QFrame *m_cameraPanel = nullptr;
    QWidget *m_cameraCell = nullptr;
    QList<PlayerStats> m_playerStats;
    QList<MatchHitRecord> m_hitRecords;
    QList<PendingVisionHit> m_pendingHits;
    QTimer *m_visionPollTimer = nullptr;
    QString m_replaySessionId;
    bool m_imuSubscribed = false;
    bool m_cameraHitSubscribed = false;
    bool m_prevYoloStable = false;
    int m_prevYoloCls = -1;
    float m_prevYoloScore = 0.0f;
signals:
    void matchEnded();
};

// ═══════════════════════════════════════════════
// 运动报告 (pageMatchReport)
// ═══════════════════════════════════════════════
class MatchReportPage : public PageBase {
    Q_OBJECT
public:
    MatchReportPage(MainWindow *mw, QWidget *parent = nullptr);
    void showReport(MatchRunningPage *running);
    QString replaySessionId() const { return m_replaySessionId; }

signals:
    /** hitIdx 为全场击球序号（从 1 起），对应回放 hit_N */
    void actionClicked(int hitIdx, int score, const QString &playerName, const QString &actionType,
                       int speedKmh, int powerTen, int durationMs);

private:
    QString m_replaySessionId;
};

// ═══════════════════════════════════════════════
// 多人/班级模式 (pageMulti)
// ═══════════════════════════════════════════════
class MultiPage : public PageBase {
    Q_OBJECT
public:
    MultiPage(MainWindow *mw, QWidget *parent = nullptr);
    void resetScan();
    QStringList m_deviceCodes;
    QList<SleSeekDevice> seekDevices() const { return m_seekDevices; }
public slots:
    void applyScanResults(const QList<SleSeekDevice> &devices);
    void onSeekStatus(const QString &msg);
    void afterScanUiReady();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private slots:
    void beginScan();
private:
    QPushButton *m_scanBtn;
    QPushButton *m_nextBtn;
    QLabel *m_scanMsg;
    QLabel *m_scanCount;
    QScrollArea *m_deviceScroll;
    QWidget *m_deviceGrid;
    int m_deviceGridCols;
    QList<SleSeekDevice> m_seekDevices;
    int computeDeviceGridCols() const;
    int computeDeviceCellWidth() const;
    void rebuildDeviceGrid();
    void updateNextButton();
signals:
    void openClassTrainNoGroup();
};

// ═══════════════════════════════════════════════
// 单人练习：扫描拍柄 + 确认 IMU 来源 (pageSinglePracticeSetup)
// ═══════════════════════════════════════════════
class SinglePracticeSetupPage : public PageBase {
    Q_OBJECT
public:
    SinglePracticeSetupPage(MainWindow *mw, QWidget *parent = nullptr);
    void resetScan();
public slots:
    void applyScanResults(const QList<SleSeekDevice> &devices);
    void onSeekStatus(const QString &msg);
    void afterScanUiReady();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private slots:
    void beginScan();
    void goConfirmStep();
    void confirmAndEnterPractice();
private:
    void showScanStep();
    void showConfirmStep();
    int computeDeviceGridCols() const;
    int computeDeviceCellWidth() const;
    void rebuildDeviceGrid();
    void rebuildConfirmList();
    void updateScanNextButton();
    QStackedWidget *m_stepStack = nullptr;
    QWidget *m_scanStep = nullptr;
    QWidget *m_confirmStep = nullptr;
    QPushButton *m_scanBtn = nullptr;
    QPushButton *m_scanNextBtn = nullptr;
    QPushButton *m_confirmBtn = nullptr;
    QLabel *m_scanMsg = nullptr;
    QLabel *m_scanCount = nullptr;
    QLabel *m_confirmHint = nullptr;
    QLabel *m_sourceLabel = nullptr;
    QScrollArea *m_deviceScroll = nullptr;
    QWidget *m_deviceGrid = nullptr;
    QScrollArea *m_confirmScroll = nullptr;
    QWidget *m_confirmList = nullptr;
    int m_deviceGridCols = 0;
    int m_selectedIndex = -1;
    QList<SleSeekDevice> m_seekDevices;
signals:
    void deviceConfirmed(int deviceId, const QString &deviceCode, const QString &deviceName,
                         const QString &mac);
};

// ═══════════════════════════════════════════════
// 设置分组 (pageGroup)
// ═══════════════════════════════════════════════
class GroupPage : public PageBase {
    Q_OBJECT
public:
    GroupPage(MainWindow *mw, QWidget *parent = nullptr);
    void openEditor(const QStringList &deviceCodes);
    QStringList m_unassigned;
    struct Bucket { QString id; QStringList codes; };
    QList<Bucket> m_buckets;
    int m_nextBucketSeq;
    QString m_selectedCode;
private:
    QLabel *m_toolbarMeta;
    QWidget *m_unassignedChips;
    QWidget *m_bucketsEl;
    void render();
signals:
    void savedAndContinue();
};

// ═══════════════════════════════════════════════
// 班级同训 (pageClassTrain)
// ═══════════════════════════════════════════════
class ClassTrainPage : public PageBase {
    Q_OBJECT
public:
    ClassTrainPage(MainWindow *mw, QWidget *parent = nullptr);
    void initFromNoGroup(const QStringList &deviceCodes, const QList<SleSeekDevice> &seekDevices);
    void initFromGrouped(const GroupPage *groupPage);
    void startMeasuring();
    void stopMeasuring();
    void appendTrainingSession(const QString &deviceCode, const QList<int> &scores,
                               const QList<int> &speedsKmh, const QList<int> &powersTen,
                               const QList<QString> &hitTypes = QList<QString>());
    struct Student {
        QString deviceCode;
        QString mac;
        QString displayName;
        QString groupTitle;
        int swings;
    };
    QList<Student> m_students;
    bool m_isGrouped;
    bool m_isMeasuring;
    qint64 m_trainStartMs = 0;
    qint64 m_trainDurationMs = 0;
    QString m_backTarget;
    QMap<QString, QList<QMap<QString, QVariant>>> m_eventsByDevice;
public slots:
    void onImuHitDetected(const QString &mac, double speedKmh, int powerTen, double peakDynG, double peakGyro,
        int durationMs, const QString &hitType, int strokeClassId, float strokeConfidence);
protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
private:
    QLabel *m_subtitleLabel_page;
    QPushButton *m_startBtn;
    QScrollArea *m_studentScroll;
    QWidget *m_scrollContent;
    bool m_imuSubscribed = false;
    void recordClassHit(const QString &deviceCode, int speedKmh, int powerTen, int score, const QString &hitType,
                        int durationMs = -1);
    QString deviceCodeForMac(const QString &mac) const;
    void syncStudentScrollContentSize();
    void renderUI();
signals:
    void studentClicked(const QString &deviceCode);
};

// ═══════════════════════════════════════════════
// 班级训练总结 (pageClassTrainSummary)
// ═══════════════════════════════════════════════
class ClassTrainSummaryPage : public PageBase {
    Q_OBJECT
public:
    ClassTrainSummaryPage(MainWindow *mw, QWidget *parent = nullptr);
    void showSummary(ClassTrainPage *ct);
    QLabel *m_studentCount;
    QLabel *m_classAvg;
    QLabel *m_totalSwings;
    QLabel *m_activeCount;
    QLabel *m_avgSwings;
    QLabel *m_speedAvg;
    QLabel *m_scoreRange;
    QLabel *m_trainDuration;
    QWidget *m_actionPanel;
    QWidget *m_listWidget;
signals:
    void studentClicked(const QString &deviceCode);
};

#endif
