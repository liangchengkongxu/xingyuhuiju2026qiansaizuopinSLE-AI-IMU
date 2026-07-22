#include <QApplication>
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPushButton>
#include <QLabel>
#include <QTimer>
#include <QDateTime>
#include <QStackedWidget>
#include <QLineEdit>
#include <QScrollArea>
#include <QFrame>
#include <QRandomGenerator>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QEvent>
#include <QByteArray>
#include <QSize>
#include <QSizePolicy>
#include <QScroller>
#include <QScrollerProperties>
#include <QShowEvent>
#include <QHideEvent>
#include <QFile>
#include <QTextStream>
#include <QMediaPlayer>
#include <QMediaPlaylist>
#include <QVideoWidget>
#include <QMediaContent>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QProcess>
#include <QDebug>
#include <QImage>
#include <QPixmap>
#include <QPalette>
#include <QColor>
#include <QPoint>
#include <QMap>
#include <QVariant>
#include <cmath>
#include <cstdlib>


#include "ui_common.h"
#include "ui_pages.h"
#include "main_window.h"
#include "class_hit_ai_advice.h"

// ═══════════════════════════════════════════════
// 主窗口 实现
// ═══════════════════════════════════════════════

void MainWindow::syncCameraVoOverlayFile()
{
#if defined(Q_OS_LINUX)
    QWidget *host = nullptr;
    const int idx = m_stack->currentIndex();
    switch (idx) {
    case 4:
        if (!m_training || !m_training->isClassStudentMode())
            host = m_training ? m_training->cameraOverlayHost() : nullptr;
        break;
    case 7:
        host = m_match ? m_match->cameraOverlayHost() : nullptr;
        break;
    case 8:
        host = m_matchRunning ? m_matchRunning->cameraOverlayHost() : nullptr;
        break;
    default:
        break;
    }

    QFile out(QStringLiteral("/tmp/.widget_cam_vo.new"));
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return;
    QTextStream ts(&out);

    const bool camPage = (idx == 4 || idx == 7 || idx == 8);
    /* 勿用 isVisible()：布局/动画期间会误判为 false，导致 VO 被反复 hidden */
    if (camPage && host && host->width() >= 32 && host->height() >= 32) {
        const QPoint g = host->mapToGlobal(QPoint(0, 0));
        int x = (g.x() / 2) * 2;
        int y = (g.y() / 2) * 2;
        int w = qMax(64, (host->width() / 2) * 2);
        int h = qMax(64, (host->height() / 2) * 2);
        ts << "1 " << x << " " << y << " " << w << " " << h << "\n";
    } else {
        /* 班级同训/扫描页等：关闭 VO 摄像头层，避免全屏预览透出 */
        ts << "0 0 0 0 0\n";
    }
    out.flush();
    out.close();
    QFile::remove(QStringLiteral("/tmp/.widget_cam_vo"));
    QFile::rename(QStringLiteral("/tmp/.widget_cam_vo.new"), QStringLiteral("/tmp/.widget_cam_vo"));
#else
    (void)0;
#endif
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent), m_screenCard(nullptr) {
    m_sleImu = new SleImuService(this);
    m_sleSeek = new SleSeekService(this);
    m_yoloAction = new WidgetYoloActionService(this);
    m_cloudUpload = new CloudUploadService(this);
    m_yoloAction->start();
    /* 面板启动后补一次预热（run.sh 已在后台 prep；此处仅 stamp 过期时触发） */
    QTimer::singleShot(12000, m_sleSeek, &SleSeekService::prepRadio);
    /* 仅 IMU 模式启动星闪桥；摄像头挥拍模式不依赖拍柄 */
    if (widgetUseImuHits())
        QTimer::singleShot(3000, m_sleImu, &SleImuService::start);
    setWindowTitle("星羽汇聚 — 板端面板");

    const QSize fbSize = linuxFbSizeFromPlatformEnv();
    setMinimumSize(320, 240);
    resize(fbSize);

    setObjectName("appRoot");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(R"(
        #appRoot {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1,
                stop:0 #030917, stop:0.45 #07142d, stop:1 #0f2450);
        }
    )");

    QPalette p;
    p.setColor(QPalette::Window, QColor("#030917"));
    p.setColor(QPalette::WindowText, QColor("#e7edf6"));
    p.setColor(QPalette::Base, QColor("#0c121b"));
    p.setColor(QPalette::Text, QColor("#e7edf6"));
    p.setColor(QPalette::Button, QColor("#0f1620"));
    p.setColor(QPalette::ButtonText, QColor("#e7edf6"));
    p.setColor(QPalette::Highlight, QColor(58, 167, 255, 64));
    p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    setPalette(p);
    setAutoFillBackground(true);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    rootLayout->addStretch(1);

    auto *midRow = new QHBoxLayout();
    midRow->setContentsMargins(0, 0, 0, 0);
    midRow->setSpacing(0);
    midRow->addStretch(1);

    m_screenCard = new QFrame(this);
    m_screenCard->setObjectName("screenCard");
    m_screenCard->setAttribute(Qt::WA_StyledBackground, true);
    m_screenCard->setStyleSheet(R"(
        QFrame#screenCard {
            border: 1px solid #1c3b75;
            border-radius: 26px;
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1,
                stop:0 rgba(10,23,49,204), stop:1 rgba(8,18,36,196));
        }
    )");
    auto *screenLay = new QVBoxLayout(m_screenCard);
    screenLay->setContentsMargins(28, 24, 28, 22);
    screenLay->setSpacing(0);

    m_stack = new QStackedWidget();
    m_stack->setObjectName("mainStack");

    m_home = new HomePage(this);                  // 0
    m_stack->addWidget(m_home);

    m_single = new SinglePage(this);              // 1
    m_stack->addWidget(m_single);

    m_practice = new PracticePage(this);           // 2
    m_stack->addWidget(m_practice);

    m_skillDetail = new SkillDetailPage(this);     // 3
    m_stack->addWidget(m_skillDetail);

    m_training = new TrainingPage(this);           // 4
    m_stack->addWidget(m_training);

    connect(m_sleImu, &SleImuService::hitDetected, m_training, &TrainingPage::onImuHitDetected,
            Qt::QueuedConnection);
    connect(m_yoloAction, &WidgetYoloActionService::actionUpdated, m_training,
            &TrainingPage::onYoloActionUpdated, Qt::QueuedConnection);
    connect(m_yoloAction, &WidgetYoloActionService::swingDetected, m_training,
            &TrainingPage::onCameraSwingDetected, Qt::QueuedConnection);
    connect(m_yoloAction, &WidgetYoloActionService::swingCountChanged, m_training,
            &TrainingPage::onSwingCountChanged, Qt::QueuedConnection);

    m_trainingSummary = new TrainingSummaryPage(this); // 5
    m_stack->addWidget(m_trainingSummary);

    m_actionDetail = new ActionDetailPage(this);    // 6
    m_stack->addWidget(m_actionDetail);

    m_match = new MatchPage(this);                 // 7
    m_stack->addWidget(m_match);

    m_matchRunning = new MatchRunningPage(this);   // 8
    m_stack->addWidget(m_matchRunning);

    connect(m_sleImu, &SleImuService::hitDetected, m_matchRunning, &MatchRunningPage::onImuHitDetected,
            Qt::QueuedConnection);
    connect(m_yoloAction, &WidgetYoloActionService::actionUpdated, m_matchRunning,
            &MatchRunningPage::onYoloActionUpdated, Qt::QueuedConnection);
    connect(m_yoloAction, &WidgetYoloActionService::swingDetected, m_matchRunning,
            &MatchRunningPage::onCameraSwingDetected, Qt::QueuedConnection);

    m_matchReport = new MatchReportPage(this);     // 9
    m_stack->addWidget(m_matchReport);

    m_multi = new MultiPage(this);                 // 10
    m_stack->addWidget(m_multi);
    connect(m_sleSeek, &SleSeekService::devicesUpdated, m_multi, &MultiPage::applyScanResults);
    connect(m_sleSeek, &SleSeekService::scanFinished, m_multi, &MultiPage::applyScanResults);
    connect(m_sleSeek, &SleSeekService::scanFinished, m_multi, &MultiPage::afterScanUiReady);
    connect(m_sleSeek, &SleSeekService::statusMessage, m_multi, &MultiPage::onSeekStatus);

    m_group = new GroupPage(this);                 // 11
    m_stack->addWidget(m_group);

    m_classTrain = new ClassTrainPage(this);       // 12
    m_stack->addWidget(m_classTrain);

    m_classTrainSummary = new ClassTrainSummaryPage(this); // 13
    m_stack->addWidget(m_classTrainSummary);

    m_singlePracticeSetup = new SinglePracticeSetupPage(this); // 14
    m_stack->addWidget(m_singlePracticeSetup);

    m_classHitDetail = new ClassHitDetailPage(this); // 15
    m_stack->addWidget(m_classHitDetail);

    m_matchSetup = new MatchSetupPage(this);       // 16
    m_stack->addWidget(m_matchSetup);

    connect(m_sleSeek, &SleSeekService::devicesUpdated, m_singlePracticeSetup,
            &SinglePracticeSetupPage::applyScanResults);
    connect(m_sleSeek, &SleSeekService::scanFinished, m_singlePracticeSetup,
            &SinglePracticeSetupPage::applyScanResults);
    connect(m_sleSeek, &SleSeekService::scanFinished, m_singlePracticeSetup,
            &SinglePracticeSetupPage::afterScanUiReady);
    connect(m_sleSeek, &SleSeekService::statusMessage, m_singlePracticeSetup,
            &SinglePracticeSetupPage::onSeekStatus);
    connect(m_singlePracticeSetup, &SinglePracticeSetupPage::deviceConfirmed, this,
            [this](int deviceId, const QString &code, const QString &name, const QString &mac) {
                beginSinglePracticeAfterBind(deviceId, code, name, mac);
            });

    connect(m_sleSeek, &SleSeekService::devicesUpdated, m_matchSetup, &MatchSetupPage::applyScanResults);
    connect(m_sleSeek, &SleSeekService::scanFinished, m_matchSetup, &MatchSetupPage::applyScanResults);
    connect(m_sleSeek, &SleSeekService::scanFinished, m_matchSetup, &MatchSetupPage::afterScanUiReady);
    connect(m_sleSeek, &SleSeekService::statusMessage, m_matchSetup, &MatchSetupPage::onSeekStatus);
    connect(m_matchSetup, &MatchSetupPage::playersConfirmed, this, &MainWindow::beginMatchAfterBind);

    screenLay->addWidget(m_stack, 1);
    midRow->addWidget(m_screenCard, 0, Qt::AlignCenter);
    midRow->addStretch(1);
    rootLayout->addLayout(midRow);
    rootLayout->addStretch(1);

    updateScreenCardSize();

    // ── 页面间导航连线 ──

    // 单人 -> 练习选择技能
    connect(m_practice, &PracticePage::skillSelected, m_skillDetail, &SkillDetailPage::setSkillName);

    // 技能详情 -> 开始练习
    connect(m_skillDetail, &SkillDetailPage::startPractice, this, [this]() {
        m_training->startTraining(m_skillDetail->m_titleLabel->text(),
                                   (int)(rand() % 20 + 1)); // practiceIndex 占位
    });

    // 训练中 -> 查看总结（仅展示，上报在退出训练时已完成）
    connect(m_training, &TrainingPage::viewSummary, this, [this]() {
        if (m_training->isClassStudentMode()) {
            const QString dc = m_training->m_classDeviceCode;
            m_classTrain->appendTrainingSession(dc, m_training->m_scores, m_training->m_speedsKmh,
                                                m_training->m_powersTen, m_training->m_hitTypes);
            m_trainingSummary->showClassStudentSummary(m_training->m_currentSkill, dc,
                                                        m_training->m_scores.size(), m_training->m_scores,
                                                        m_training->m_speedsKmh, m_training->m_powersTen,
                                                        m_training->m_hitTypes);
            m_training->stopTraining();
            switchPage(5);
            return;
        }
        m_trainingSummary->showSummary(m_training->m_currentSkill, m_training->m_practiceIndex,
                                       m_training->m_scores, m_training->m_speedsKmh,
                                       m_training->m_powersTen);
        switchPage(5);
    });

    // 单人练习：点「退出训练」即上报云端，不等到打开训练记录
    connect(m_training, &TrainingPage::exitTraining, this, [this]() {
        uploadSoloDrillIfNeeded();
    });

    // 总结 -> 动作详情（单人训练；班级走 classHitClicked -> page 15）
    connect(m_trainingSummary, &TrainingSummaryPage::actionClicked, this, [this](int idx, int score) {
        if (m_trainingSummary->m_source == "classTrain")
            return;
        QString label = m_training->m_currentSkill;
        const int speedKmh = m_training->speedAt(idx);
        const int powerTen = m_training->powerAt(idx);
        const int durationMs = m_training->durationAt(idx);
        const QString hitType = m_training->hitTypeAt(idx);
        m_actionDetail->showAction(idx, score, label, speedKmh, powerTen, m_training->replaySessionId(), durationMs,
                                   hitType);
        switchPage(6);
    });

    connect(m_trainingSummary, &TrainingSummaryPage::classHitClicked, this,
            [this](int idx, int score, const QString &hitType, int speedKmh, int powerTen, int durationMs) {
                m_classHitDetail->showHit(idx, m_trainingSummary->m_classStudentName, hitType, score, speedKmh,
                                          powerTen, durationMs);
                switchPage(15);
            });

    // 比赛 -> 比赛开始
    connect(m_match, &MatchPage::startMatch, this, [this]() {
        if (m_matchRunning)
            m_matchRunning->initPlayers(m_matchPlayers);
        if (m_matchRunning)
            m_matchRunning->startRunning();
    });

    // 比赛结束 -> 报告
    connect(m_matchRunning, &MatchRunningPage::matchEnded, this, [this]() {
        if (m_cloudUpload && m_cloudUpload->enabled() && m_matchRunning) {
            QList<CloudMatchStroke> strokes;
            for (const auto &rec : m_matchRunning->hitRecords()) {
                CloudMatchStroke st;
                st.actionType = rec.actionType;
                st.score = rec.score;
                st.aiSuggestion = rec.aiSuggestion;
                st.ballSpeedKmh = rec.speedKmh;
                st.powerTen = rec.powerTen;
                strokes.append(st);
            }
            const qint64 durationMs = std::max<qint64>(
                0, (m_matchRunning->m_endedAt ? m_matchRunning->m_endedAt : QDateTime::currentMSecsSinceEpoch())
                       - m_matchRunning->m_startedAt);
            const int durationMin = static_cast<int>(durationMs / 60000);
            m_cloudUpload->uploadMatch(QStringLiteral("板端对打"), durationMin, strokes);
        }
        m_matchReport->showReport(m_matchRunning);
        switchPage(9);
    });

    connect(m_matchReport, &MatchReportPage::actionClicked, this,
            [this](int hitIdx, int score, const QString &playerName, const QString &actionType, int speedKmh,
                   int powerTen, int durationMs) {
                const QString label = playerName.isEmpty() ? QStringLiteral("比赛击球") : playerName;
                const QString type = actionType.trimmed().isEmpty() ? QStringLiteral("挥拍") : actionType.trimmed();
                m_actionDetail->showAction(hitIdx, score, label, speedKmh, powerTen,
                                           m_matchReport->replaySessionId(), durationMs, type, 9);
                switchPage(6);
            });

    // 多人 -> 班级训练（设置分组页暂不开放）
    connect(m_multi, &MultiPage::openClassTrainNoGroup, this, [this]() {
        beginClassTrainFromMulti(m_multi->m_deviceCodes, m_multi->seekDevices());
    });

    connect(m_sleImu, &SleImuService::hitDetected, m_classTrain, &ClassTrainPage::onImuHitDetected,
            Qt::QueuedConnection);

    // 分组 -> 保存并继续
    connect(m_group, &GroupPage::savedAndContinue, this, [this]() {
        m_classTrain->initFromGrouped(m_group);
        switchPage(12);
        QTimer::singleShot(800, this, [this]() {
            if (m_sleImu) {
                m_sleImu->resetSwingDetector();
                m_sleImu->start();
            }
            if (m_classTrain)
                m_classTrain->startMeasuring();
        });
    });

    // 班级 -> 学员：查看 IMU 挥拍记录（不进入摄像头训练页）
    connect(m_classTrain, &ClassTrainPage::studentClicked, this, [this](const QString &deviceCode) {
        QString name;
        for (const auto &st : m_classTrain->m_students) {
            if (st.deviceCode == deviceCode) {
                name = st.displayName;
                break;
            }
        }
        const auto events = m_classTrain->m_eventsByDevice.value(deviceCode);
        QList<int> scores, speeds, powers, durations;
        QList<QString> hitTypes;
        for (const auto &ev : events) {
            scores.append(ev.value(QStringLiteral("score")).toInt());
            hitTypes.append(ev.value(QStringLiteral("hitType")).toString());
            if (ev.contains(QStringLiteral("speedKmh")))
                speeds.append(ev.value(QStringLiteral("speedKmh")).toInt());
            if (ev.contains(QStringLiteral("power")))
                powers.append(ev.value(QStringLiteral("power")).toInt());
            durations.append(ev.contains(QStringLiteral("durationMs"))
                                 ? ev.value(QStringLiteral("durationMs")).toInt()
                                 : -1);
        }
        m_trainingSummary->showClassStudentSummary(name, deviceCode, events.size(),
                                                   scores, speeds, powers, hitTypes, durations);
        switchPage(5);
    });

    // 班级总结 -> 学员：同上
    connect(m_classTrainSummary, &ClassTrainSummaryPage::studentClicked, this, [this](const QString &deviceCode) {
        QString name;
        for (const auto &st : m_classTrain->m_students) {
            if (st.deviceCode == deviceCode) {
                name = st.displayName;
                break;
            }
        }
        const auto events = m_classTrain->m_eventsByDevice.value(deviceCode);
        QList<int> scores, speeds, powers, durations;
        QList<QString> hitTypes;
        for (const auto &ev : events) {
            scores.append(ev.value(QStringLiteral("score")).toInt());
            hitTypes.append(ev.value(QStringLiteral("hitType")).toString());
            if (ev.contains(QStringLiteral("speedKmh")))
                speeds.append(ev.value(QStringLiteral("speedKmh")).toInt());
            if (ev.contains(QStringLiteral("power")))
                powers.append(ev.value(QStringLiteral("power")).toInt());
            durations.append(ev.contains(QStringLiteral("durationMs"))
                                 ? ev.value(QStringLiteral("durationMs")).toInt()
                                 : -1);
        }
        m_trainingSummary->showClassStudentSummary(name, deviceCode, events.size(),
                                                   scores, speeds, powers, hitTypes, durations);
        switchPage(5);
    });


    switchPage(0);

    m_cameraVoTimer = new QTimer(this);
    m_cameraVoTimer->setInterval(80);
    connect(m_cameraVoTimer, &QTimer::timeout, this, [this]() { syncCameraVoOverlayFile(); });
    m_cameraVoTimer->start();
    connect(m_stack, &QStackedWidget::currentChanged, this, [this](int) { syncCameraVoOverlayFile(); });
}

void MainWindow::updateScreenCardSize() {
    if (!m_screenCard)
        return;
    /* 占满可用区域（留边），保留与 HTML 原型相近的宽扁比例，便于大屏触控 */
    const int margin = 20;
    const int maxW = qMax(320, width() - margin * 2);
    const int maxH = qMax(200, height() - margin * 2);
    constexpr double kAspect = 1080.0 / 600.0;
    int w = maxW;
    int h = int(qRound(w / kAspect));
    if (h > maxH) {
        h = maxH;
        w = int(qRound(h * kAspect));
    }
    if (w > maxW) {
        w = maxW;
        h = int(qRound(w / kAspect));
    }
    m_screenCard->setFixedSize(qMax(320, w), qMax(200, h));
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    updateScreenCardSize();
    syncCameraVoOverlayFile();
}

void MainWindow::switchPage(int index) {
    const int prev = m_stack ? m_stack->currentIndex() : -1;
    if (index >= 0 && index < m_stack->count()) {
        const bool toSeekPage = (index == 10 || index == 14 || index == 16);
        const bool fromSeekPage = (prev == 10 || prev == 14 || prev == 16);
        if (toSeekPage && !fromSeekPage) {
            if (m_sleImu)
                m_sleImu->stop();
            if (m_sleSeek)
                QTimer::singleShot(200, m_sleSeek, &SleSeekService::prepRadio);
        } else if (fromSeekPage && !toSeekPage) {
            if (m_sleSeek && m_sleSeek->isScanning())
                m_sleSeek->stopScan();
            const bool practiceFlow = (index >= 2 && index <= 4);
            const bool imuFlow = (practiceFlow || index == 5 || index == 7 || index == 8 || index == 12 || index == 15);
            if (m_sleImu && imuFlow && !m_sleImu->isActive())
                QTimer::singleShot(400, m_sleImu, &SleImuService::start);
        } else if (index == 12 && m_classTrain && m_classTrain->m_isMeasuring) {
            if (m_sleImu && !m_sleImu->isActive())
                QTimer::singleShot(200, m_sleImu, &SleImuService::start);
            m_classTrain->startMeasuring();
        }
        m_stack->setCurrentIndex(index);
    }
    /* 回放采集可能在离开训练页后仍进行中，勿在此处 clearReplaySession（见 goHome） */
    if (index == 13 && m_classTrainSummary && m_classTrain)
        m_classTrainSummary->showSummary(m_classTrain);
    if (m_training)
        m_training->enableCameraHitListen(index == 4 && !m_training->isClassStudentMode());
    if (m_matchRunning)
        m_matchRunning->enableCameraHitListen(index == 7 || index == 8);
    if (index == 8 && m_sleImu && !m_sleImu->isActive()) {
        QTimer::singleShot(300, this, [this]() {
            if (m_sleImu) {
                m_sleImu->resetSwingDetector();
                m_sleImu->start();
            }
        });
    }
    syncCameraVoOverlayFile();
    /* 页面切换后布局可能尚未稳定，延迟再同步一次 VO 窗口 */
    if (index == 4 || index == 7 || index == 8) {
        QTimer::singleShot(180, this, [this]() { syncCameraVoOverlayFile(); });
    }
}

void MainWindow::uploadSoloDrillIfNeeded()
{
    if (!m_training || m_training->isClassStudentMode() || m_training->m_scores.isEmpty())
        return;
    if (!m_cloudUpload || !m_cloudUpload->enabled())
        return;
    QList<CloudDrillHit> hits;
    for (int i = 0; i < m_training->m_scores.size(); ++i) {
        CloudDrillHit h;
        h.skillName = m_training->m_currentSkill;
        h.score = m_training->m_scores.at(i);
        h.ballSpeedKmh = i < m_training->m_speedsKmh.size() ? m_training->m_speedsKmh.at(i) : 0;
        h.powerTen = i < m_training->m_powersTen.size() ? m_training->m_powersTen.at(i) : 0;
        ClassHitAdviceContext ctx;
        ctx.hitIdx = i + 1;
        ctx.hitType = m_training->hitTypeAt(i + 1);
        if (ctx.hitType.trimmed().isEmpty())
            ctx.hitType = m_training->m_currentSkill;
        ctx.score = h.score;
        ctx.speedKmh = h.ballSpeedKmh;
        ctx.powerTen = h.powerTen;
        ctx.durationMs = m_training->durationAt(i + 1);
        h.aiSuggestion = pickClassHitAiAdvice(ctx);
        hits.append(h);
    }
    m_cloudUpload->uploadDrillSession(hits);
}

void MainWindow::goHome() {
    if (m_training)
        m_training->stopTraining();
    if (m_matchRunning)
        m_matchRunning->stopRunning();
    if (m_classTrain)
        m_classTrain->stopMeasuring();
    switchPage(0);
    clearReplaySession();
}

void MainWindow::beginClassTrainFromMulti(const QStringList &deviceCodes,
                                          const QList<SleSeekDevice> &seekDevices)
{
    if (m_sleSeek && m_sleSeek->isScanning())
        m_sleSeek->stopScan();

    const QString seekBridge = QStringLiteral("/opt/widget_ui/ws73/sle_seek_bridge.sh");
    if (QFile::exists(seekBridge))
        QProcess::execute(QStringLiteral("/bin/sh"), {seekBridge, QStringLiteral("stop")});

    m_classTrain->initFromNoGroup(deviceCodes, seekDevices);
    switchPage(12);

    QTimer::singleShot(800, this, [this]() {
        if (m_sleImu) {
            m_sleImu->resetSwingDetector();
            m_sleImu->start();
        }
        if (m_classTrain)
            m_classTrain->startMeasuring();
    });
}

void MainWindow::beginMatchAfterBind(const QList<MatchPlayerBinding> &players)
{
    if (m_sleSeek && m_sleSeek->isScanning())
        m_sleSeek->stopScan();

    m_matchPlayers = players;
    if (m_match)
        m_match->setConnectedPlayers(m_matchPlayers);
    switchPage(7);

    QTimer::singleShot(500, this, [this]() {
        if (m_sleImu) {
            m_sleImu->resetSwingDetector();
            m_sleImu->start();
        }
    });
}

void MainWindow::beginSinglePracticeAfterBind(int deviceId, const QString &deviceCode,
                                              const QString &deviceName, const QString &mac)
{
    if (m_sleSeek && m_sleSeek->isScanning())
        m_sleSeek->stopScan();

    m_singlePracticeDeviceId = deviceId;
    m_singlePracticeDeviceCode = deviceCode;
    m_singlePracticeDeviceName = deviceName;
    m_singlePracticeDeviceMac = mac;

    switchPage(2);

    QTimer::singleShot(300, this, [this]() {
        if (m_sleImu) {
            m_sleImu->resetSwingDetector();
            m_sleImu->start();
        }
    });
}

#include "main_window.moc"
