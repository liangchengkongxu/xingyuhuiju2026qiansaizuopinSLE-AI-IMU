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
// 训练中 实现
// ═══════════════════════════════════════════════
TrainingPage::TrainingPage(MainWindow *mw, QWidget *parent)
    : PageBase("训练中", "", mw, parent, PageHeaderMode::SingleCentered), m_practiceIndex(0)
{
    connect(m_homeBtn, &QPushButton::clicked, this, [this, mw]() {
        stopTraining();
        mw->goHome();
    });
    m_backBtn->hide();

    /* 两栏：左 KPI + 纠正 + 底栏按钮；右摄像头加大并靠右 */
    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(0);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);

    static const char *kPanelStyle =
        "QFrame#trainPanel { border: 1px solid #234f8c; border-radius: 16px; "
        "background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(10,26,51,199), stop:1 rgba(8,22,45,209)); }";

    auto makeTrainMetricChip = [this](const QString &title, QLabel *&value, const QString &unitHint = QString()) -> QFrame * {
        auto *f = new QFrame();
        f->setObjectName("trainMetricChip");
        f->setMinimumHeight(96);
        f->setStyleSheet(
            "QFrame#trainMetricChip { border: 1px solid #2e63ac; border-radius: 14px; background: rgba(7,23,47,200); }");
        m_metricChips.append(f);
        auto *fl = new QVBoxLayout(f);
        fl->setContentsMargins(14, 12, 14, 12);
        fl->setSpacing(4);
        auto *tl = new QLabel(title);
        tl->setObjectName("trainMetricTitle");
        tl->setStyleSheet("font-size:14px; color:#8cc7ff; font-weight:700; background:transparent;");
        fl->addWidget(tl);
        value = new QLabel(QStringLiteral("--"));
        value->setObjectName("trainMetricValue");
        value->setStyleSheet("font-size:28px; font-weight:900; color:#eaf3ff; background:transparent;");
        fl->addWidget(value);
        if (!unitHint.isEmpty()) {
            auto *ul = new QLabel(unitHint);
            ul->setObjectName("trainMetricUnit");
            ul->setStyleSheet("font-size:13px; color:#9eb7de; background:transparent;");
            fl->addWidget(ul);
        }
        return f;
    };

    // ── 左列：打分/指标（偏下）+ 纠正 + 左下操作按钮
    auto *scorePanel = new QFrame();
    m_scorePanel = scorePanel;
    scorePanel->setObjectName("trainPanel");
    scorePanel->setMinimumHeight(360);
    scorePanel->setStyleSheet(kPanelStyle);
    auto *spLay = new QVBoxLayout(scorePanel);
    spLay->setContentsMargins(16, 16, 16, 16);
    spLay->setSpacing(12);

    auto *kpiRow = new QGridLayout();
    kpiRow->setContentsMargins(0, 0, 0, 0);
    kpiRow->setHorizontalSpacing(12);
    kpiRow->setColumnStretch(0, 5);
    kpiRow->setColumnStretch(1, 6);

    auto *kpiCard = new QFrame();
    m_kpiCard = kpiCard;
    kpiCard->setObjectName("trainKpiCard");
    kpiCard->setMinimumHeight(200);
    kpiCard->setStyleSheet(
        "QFrame#trainKpiCard { border: 1px solid #2e63ac; border-radius: 14px; background: rgba(7,23,47,210); }");
    auto *kpiLay = new QVBoxLayout(kpiCard);
    kpiLay->setContentsMargins(16, 14, 16, 14);
    kpiLay->setSpacing(6);
    m_aiScoreTitle = new QLabel(QStringLiteral("AI 打分"));
    m_aiScoreTitle->setStyleSheet("color:#8cc7ff; font-size:18px; font-weight:700; background:transparent;");
    kpiLay->addWidget(m_aiScoreTitle);

    auto *scoreRow = new QHBoxLayout();
    scoreRow->setSpacing(8);
    m_aiScoreValue = new QLabel(QStringLiteral("--"));
    m_aiScoreValue->setStyleSheet("font-size:80px; font-weight:900; line-height:0.95; color:#eaf3ff; background:transparent;");
    m_scoreUnit = new QLabel(QStringLiteral("分"));
    m_scoreUnit->setStyleSheet("color:#b9cff1; font-size:26px; font-weight:700; background:transparent;");
    scoreRow->addWidget(m_aiScoreValue, 0, Qt::AlignBottom);
    scoreRow->addWidget(m_scoreUnit, 0, Qt::AlignBottom);
    scoreRow->addStretch();
    kpiLay->addLayout(scoreRow);
    kpiLay->addStretch();

    auto *metricsWrap = new QWidget();
    auto *metricsGrid = new QGridLayout(metricsWrap);
    metricsGrid->setContentsMargins(0, 0, 0, 0);
    metricsGrid->setSpacing(10);
    metricsGrid->addWidget(makeTrainMetricChip(QStringLiteral("击球次数"), m_hitCountLabel, QStringLiteral("次")), 0, 0);
    metricsGrid->addWidget(makeTrainMetricChip(QStringLiteral("球速"), m_speedLabel, QStringLiteral("km/h")), 0, 1);
    metricsGrid->addWidget(makeTrainMetricChip(QStringLiteral("击球力度"), m_powerLabel, QStringLiteral("/10")), 1, 0);
    metricsGrid->addWidget(makeTrainMetricChip(QStringLiteral("平均球速"), m_avgSpeedLabel, QStringLiteral("km/h")), 1, 1);
    metricsGrid->addWidget(makeTrainMetricChip(QStringLiteral("击球类型"), m_actionTypeLabel, QString()), 2, 0, 1, 2);
    if (m_actionTypeLabel)
        m_actionTypeLabel->setText(QStringLiteral("--"));

    kpiRow->addWidget(kpiCard, 0, 0, Qt::AlignTop);
    kpiRow->addWidget(metricsWrap, 0, 1, Qt::AlignTop);
    spLay->addLayout(kpiRow);

    m_aiScoreHint = new QLabel(QStringLiteral("等待开始…"));
    m_aiScoreHint->setStyleSheet("color:#cfe2ff; font-size:16px; line-height:1.45; background:transparent;");
    m_aiScoreHint->setWordWrap(true);
    spLay->addWidget(m_aiScoreHint);

    auto *corrPanel = new QFrame();
    m_corrPanel = corrPanel;
    corrPanel->setObjectName("trainPanel");
    corrPanel->setMinimumHeight(200);
    corrPanel->setStyleSheet(kPanelStyle);
    auto *cpLay = new QVBoxLayout(corrPanel);
    cpLay->setContentsMargins(16, 14, 16, 14);
    cpLay->setSpacing(8);
    auto *cpTitle = new QLabel(QStringLiteral("动作纠正"));
    cpTitle->setStyleSheet("color:#8cc7ff; font-size:17px; font-weight:700; background:transparent;");
    cpLay->addWidget(cpTitle);

    m_correctionText = new QLabel(QStringLiteral("这里显示纠正建议（占位）。"));
    m_correctionText->setStyleSheet("color:#d9e9ff; font-size:16px; line-height:1.55; background:transparent;");
    m_correctionText->setWordWrap(true);
    cpLay->addWidget(m_correctionText, 1);

    m_leftCol = new QWidget();
    auto *leftCol = m_leftCol;
    leftCol->setMinimumWidth(kTrainLeftColW);
    leftCol->setMaximumWidth(kTrainLeftColW + 48);
    auto *lv = new QVBoxLayout(leftCol);
    lv->setContentsMargins(0, 36, 0, 4);
    lv->setSpacing(12);
    lv->addStretch(2);
    lv->addWidget(scorePanel, 5);
    lv->addWidget(corrPanel, 4);

    auto *btnPanel = new QWidget();
    auto *btnLay = new QVBoxLayout(btnPanel);
    btnLay->setContentsMargins(0, 4, 0, 0);
    btnLay->setSpacing(10);
    auto *recordBtn = new QPushButton(QStringLiteral("本次训练记录"));
    recordBtn->setCursor(Qt::PointingHandCursor);
    recordBtn->setFixedHeight(76);
    recordBtn->setStyleSheet(R"(
        QPushButton {
            border: 1px solid #2f6ab4; border-radius: 16px;
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #153a6f, stop:1 #0f2d56);
            color: #eaf3ff; font-size: 19px; font-weight: 700;
        }
        QPushButton:hover { border-color: #56baff; }
    )");
    connect(recordBtn, &QPushButton::clicked, this, [this]() {
        stopTraining();
        emit viewSummary();
    });
    btnLay->addWidget(recordBtn);

    auto *exitBtn = new QPushButton(QStringLiteral("退出训练"));
    exitBtn->setCursor(Qt::PointingHandCursor);
    exitBtn->setFixedHeight(76);
    exitBtn->setStyleSheet(R"(
        QPushButton {
            border: 1px solid #7a2b2b; border-radius: 16px;
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #5a1f1f, stop:1 #3a1414);
            color: #fff3f3; font-size: 19px; font-weight: 700;
        }
        QPushButton:hover { border-color: #ff6b6b; }
    )");
    connect(exitBtn, &QPushButton::clicked, this, [this, mw]() {
        const bool classMode = m_classStudentMode;
        const QString deviceCode = m_classDeviceCode;
        stopTraining();
        if (classMode) {
            if (mw && mw->m_classTrain)
                mw->m_classTrain->appendTrainingSession(deviceCode, m_scores, m_speedsKmh, m_powersTen);
            mw->switchPage(12);
        } else {
            emit exitTraining();
            mw->switchPage(3);
        }
    });
    btnLay->addWidget(exitBtn);
    lv->addWidget(btnPanel, 0);

    // ── 右列：摄像头加大，水平靠右、垂直居中（班级学员页隐藏）
    auto *cameraPanel = new QFrame();
    m_cameraPanel = cameraPanel;
    cameraPanel->setObjectName("trainCam");
    m_cameraOverlayHost = cameraPanel;
    cameraPanel->setAttribute(Qt::WA_TranslucentBackground, false);
    cameraPanel->setFixedSize(kCamTrainingW, kCamTrainingH);
    cameraPanel->setStyleSheet(
        "QFrame#trainCam { border: 1px solid #234f8c; border-radius: 16px; background: #000000; }");

    m_cameraCell = new QWidget();
    auto *camCellLay = new QHBoxLayout(m_cameraCell);
    camCellLay->setContentsMargins(0, 0, 0, 0);
    camCellLay->setSpacing(0);
    camCellLay->addStretch(1);
    camCellLay->addWidget(cameraPanel, 0, Qt::AlignRight | Qt::AlignVCenter);

    m_trainGrid = grid;
    grid->addWidget(leftCol, 0, 0, Qt::AlignTop);
    grid->addWidget(m_cameraCell, 0, 1, Qt::AlignRight | Qt::AlignVCenter);

    m_rootLayout->addLayout(grid, 1);
    applyTrainLayout();
}

void TrainingPage::applyTrainMetricsStyle(bool enlarged)
{
    const int scoreFs = enlarged ? 102 : 80;
    const int scoreUnitFs = enlarged ? 33 : 26;
    const int scoreTitleFs = enlarged ? 23 : 18;
    const int hintFs = enlarged ? 20 : 16;
    const int chipTitleFs = enlarged ? 18 : 14;
    const int chipValueFs = enlarged ? 36 : 28;
    const int chipUnitFs = enlarged ? 17 : 13;
    const int chipMinH = enlarged ? 122 : 96;
    const int kpiMinH = enlarged ? 256 : 200;
    const int panelMinH = enlarged ? 460 : 360;

    if (m_aiScoreValue)
        m_aiScoreValue->setStyleSheet(QStringLiteral(
            "font-size:%1px; font-weight:900; line-height:0.95; color:#eaf3ff; background:transparent;").arg(scoreFs));
    if (m_scoreUnit)
        m_scoreUnit->setStyleSheet(QStringLiteral(
            "color:#b9cff1; font-size:%1px; font-weight:700; background:transparent;").arg(scoreUnitFs));
    if (m_aiScoreTitle)
        m_aiScoreTitle->setStyleSheet(QStringLiteral(
            "color:#8cc7ff; font-size:%1px; font-weight:700; background:transparent;").arg(scoreTitleFs));
    if (m_aiScoreHint)
        m_aiScoreHint->setStyleSheet(QStringLiteral(
            "color:#cfe2ff; font-size:%1px; line-height:1.45; background:transparent;").arg(hintFs));
    if (m_kpiCard)
        m_kpiCard->setMinimumHeight(kpiMinH);
    if (m_scorePanel)
        m_scorePanel->setMinimumHeight(panelMinH);

    for (QFrame *chip : m_metricChips) {
        if (!chip)
            continue;
        chip->setMinimumHeight(chipMinH);
        const auto titles = chip->findChildren<QLabel *>(QStringLiteral("trainMetricTitle"));
        const auto values = chip->findChildren<QLabel *>(QStringLiteral("trainMetricValue"));
        const auto units = chip->findChildren<QLabel *>(QStringLiteral("trainMetricUnit"));
        for (QLabel *lb : titles)
            lb->setStyleSheet(QStringLiteral(
                "font-size:%1px; color:#8cc7ff; font-weight:700; background:transparent;").arg(chipTitleFs));
        for (QLabel *lb : values)
            lb->setStyleSheet(QStringLiteral(
                "font-size:%1px; font-weight:900; color:#eaf3ff; background:transparent;").arg(chipValueFs));
        for (QLabel *lb : units)
            lb->setStyleSheet(QStringLiteral(
                "font-size:%1px; color:#9eb7de; background:transparent;").arg(chipUnitFs));
    }
}

void TrainingPage::applyTrainLayout()
{
    if (!m_trainGrid || !m_leftCol)
        return;
    if (m_classStudentMode) {
        m_cameraCell->hide();
        m_cameraOverlayHost = nullptr;
        m_leftCol->setMinimumWidth(720);
        m_leftCol->setMaximumWidth(QWIDGETSIZE_MAX);
        m_trainGrid->setColumnStretch(0, 1);
        m_trainGrid->setColumnStretch(1, 0);
        if (m_corrPanel)
            m_corrPanel->show();
        applyTrainMetricsStyle(false);
    } else {
        m_cameraCell->show();
        m_cameraOverlayHost = m_cameraPanel;
        m_leftCol->setMinimumWidth(kTrainLeftColW);
        m_leftCol->setMaximumWidth(kTrainLeftColW + 48);
        m_trainGrid->setColumnStretch(0, 0);
        m_trainGrid->setColumnStretch(1, 1);
        if (m_corrPanel)
            m_corrPanel->hide();
        applyTrainMetricsStyle(true);
    }
}

int TrainingPage::speedAt(int oneBasedIdx) const
{
    if (oneBasedIdx < 1 || oneBasedIdx > m_speedsKmh.size())
        return -1;
    return m_speedsKmh.at(oneBasedIdx - 1);
}

int TrainingPage::powerAt(int oneBasedIdx) const
{
    if (oneBasedIdx < 1 || oneBasedIdx > m_powersTen.size())
        return -1;
    return m_powersTen.at(oneBasedIdx - 1);
}

QString TrainingPage::hitTypeAt(int oneBasedIdx) const
{
    if (oneBasedIdx < 1 || oneBasedIdx > m_hitTypes.size())
        return QString();
    return m_hitTypes.at(oneBasedIdx - 1);
}

int TrainingPage::durationAt(int oneBasedIdx) const
{
    if (oneBasedIdx < 1 || oneBasedIdx > m_durationsMs.size())
        return -1;
    return m_durationsMs.at(oneBasedIdx - 1);
}

QString TrainingPage::replayPathAt(int oneBasedIdx) const
{
    return resolveReplayClip(m_replaySessionId, oneBasedIdx);
}

void TrainingPage::applyHitAdvice(int hitIdx, const QString &hitType, int score, int speedKmh, int powerTen,
                                  int durationMs)
{
    if (m_classStudentMode) {
        if (!m_correctionText)
            return;
        ClassHitAdviceContext ctx;
        ctx.hitIdx = hitIdx;
        ctx.hitType = hitType.trimmed().isEmpty() ? QStringLiteral("挥拍") : hitType.trimmed();
        ctx.score = score;
        ctx.speedKmh = speedKmh;
        ctx.powerTen = powerTen;
        ctx.durationMs = durationMs;
        m_correctionText->setText(pickClassHitAiAdvice(ctx));
    }
}

void TrainingPage::registerHitReplay(int hitIdx)
{
    if (m_classStudentMode || m_replaySessionId.isEmpty() || hitIdx <= 0)
        return;
    requestHitReplayCapture(m_replaySessionId, hitIdx);
}

void TrainingPage::refreshFixedSkillTypeLabel()
{
    if (!m_actionTypeLabel || m_classStudentMode)
        return;
    m_actionTypeLabel->setText(m_currentSkill.isEmpty() ? QStringLiteral("--") : m_currentSkill);
}

void TrainingPage::updateActionTypeLabel(int clsId, const QString &nameCn, float score, bool stable)
{
    if (!m_actionTypeLabel)
        return;
    if (!m_classStudentMode) {
        refreshFixedSkillTypeLabel();
        return;
    }
    if (clsId < 0 || nameCn.isEmpty() || nameCn == QStringLiteral("无")) {
        m_actionTypeLabel->setText(QStringLiteral("--"));
        return;
    }
    const QString tag = stable ? nameCn : QStringLiteral("%1?").arg(nameCn);
    if (score > 0.01f)
        m_actionTypeLabel->setText(QStringLiteral("%1").arg(tag));
    else
        m_actionTypeLabel->setText(tag);
}

void TrainingPage::onYoloActionUpdated(int clsId, const QString &nameCn, const QString &, float score, bool stable)
{
    updateActionTypeLabel(clsId, nameCn, score, stable);

    if (m_classStudentMode || !m_cameraHitSubscribed)
        return;

    const float minStable = practiceCameraStableConf();
    const bool becameStableHigh = stable && clsId >= 0 && score >= minStable &&
        (!m_prevYoloStable || m_prevYoloCls != clsId || m_prevYoloScore < minStable);
    m_prevYoloStable = stable;
    m_prevYoloCls = clsId;
    m_prevYoloScore = score;

    if (!becameStableHigh)
        return;
    if (!tryAcceptPracticeHit(QStringLiteral("cam_stable")))
        return;

    const QString detail = QStringLiteral("摄像头稳定识别 %1%")
                               .arg(static_cast<int>(score * 100.0f + 0.5f));
    onCameraHit(clsId, nameCn, score, detail);
}

void TrainingPage::onCameraHit(int clsId, const QString &nameCn, float score, const QString &detail)
{
    Q_UNUSED(clsId);
    const QString displayType = m_classStudentMode ? nameCn : m_currentSkill;
    const int aiScore = qBound(45, static_cast<int>(score * 100.0f + 0.5f), 99);
    m_scores.append(aiScore);
    m_speedsKmh.append(-1);
    m_powersTen.append(-1);
    m_hitTypes.append(displayType);
    m_durationsMs.append(-1);

    int sum = 0;
    for (int s : m_scores)
        sum += s;
    m_aiScoreValue->setText(m_scores.isEmpty() ? QStringLiteral("--")
                                                : QString::number(sum / m_scores.size()));

    const int hits = m_scores.size();
    if (m_hitCountLabel)
        m_hitCountLabel->setText(QString::number(hits));
    if (m_speedLabel)
        m_speedLabel->setText(QStringLiteral("—"));
    if (m_powerLabel)
        m_powerLabel->setText(QStringLiteral("—"));
    if (m_avgSpeedLabel)
        m_avgSpeedLabel->setText(QStringLiteral("—"));
    updateActionTypeLabel(clsId, nameCn, score, true);
    applyHitAdvice(hits, displayType, aiScore, -1, -1, -1);
    registerHitReplay(hits);
    if (m_classStudentMode) {
        m_aiScoreHint->setText(QStringLiteral("学员「%1」· %2 · 摄像头已识别 %3 次挥拍")
                                   .arg(m_currentSkill)
                                   .arg(displayType)
                                   .arg(hits));
    } else {
        m_aiScoreHint->setText(QStringLiteral("第 %1 次练习「%2」· 摄像头已识别 %3 次挥拍")
                                   .arg(m_practiceIndex)
                                   .arg(m_currentSkill)
                                   .arg(hits));
    }
}

void TrainingPage::refreshCameraSwingCountHint(int sessionCount, const QString &lastHitName)
{
    const QString hitName = m_classStudentMode
        ? (lastHitName.isEmpty() ? QStringLiteral("—") : lastHitName)
        : (m_currentSkill.isEmpty() ? QStringLiteral("—") : m_currentSkill);
    if (m_classStudentMode) {
        m_aiScoreHint->setText(QStringLiteral("学员「%1」· %2 · 摄像头已识别 %3 次挥拍")
                                   .arg(m_currentSkill)
                                   .arg(hitName)
                                   .arg(sessionCount));
    } else {
        m_aiScoreHint->setText(QStringLiteral("第 %1 次练习「%2」· %3 · 摄像头已识别 %4 次挥拍")
                                   .arg(m_practiceIndex)
                                   .arg(m_currentSkill)
                                   .arg(hitName)
                                   .arg(sessionCount));
    }
}

void TrainingPage::onSwingCountChanged(int sessionCount)
{
    if (!m_cameraHitSubscribed)
        return;
    if (m_hitCountLabel)
        m_hitCountLabel->setText(QString::number(sessionCount));
    refreshCameraSwingCountHint(sessionCount, m_lastSwingHitName);
}

void TrainingPage::onCameraSwingDetected(int swingSeq, int clsId, const QString &nameCn, float score)
{
    Q_UNUSED(swingSeq);
    if (!m_cameraHitSubscribed)
        return;
    if (score < practiceCameraMinSwingScore())
        return;
    if (!tryAcceptPracticeHit(QStringLiteral("cam_swing")))
        return;
    m_lastSwingHitName = nameCn;
    const QString detail = QStringLiteral("摄像头挥拍 %1%")
                               .arg(static_cast<int>(score * 100.0f + 0.5f));
    onCameraHit(clsId, nameCn, score, detail);
    if (m_mainWindow && m_mainWindow->yoloAction())
        onSwingCountChanged(m_mainWindow->yoloAction()->sessionSwingCount());
}

void TrainingPage::onImuHit(double speedKmh, int powerTen, int presetScore, const QString &hitType, int durationMs,
                            const QString &sourceHint)
{
    const int speed = displaySpeedKmh(speedKmh);
    const int power = qBound(1, powerTen, 10);
    m_speedsKmh.append(speed);
    m_powersTen.append(power);
    m_hitTypes.append(hitType.trimmed().isEmpty() ? QStringLiteral("挥拍") : hitType.trimmed());
    m_durationsMs.append(durationMs >= 0 ? durationMs : -1);
    m_speedSum += speed;
    m_speedCount++;
    m_powerSum += power;
    m_powerCount++;
    const int score = presetScore >= 0 ? presetScore : randInt(55, 98);
    m_scores.append(score);

    int sum = 0;
    for (int s : m_scores)
        sum += s;
    m_aiScoreValue->setText(m_scores.isEmpty() ? QStringLiteral("--")
                                                : QString::number(sum / m_scores.size()));

    const int hits = m_speedsKmh.size();
    const int avg = m_speedCount > 0 ? m_speedSum / m_speedCount : 0;
    if (m_hitCountLabel)
        m_hitCountLabel->setText(QStringLiteral("%1").arg(hits));
    if (m_speedLabel)
        m_speedLabel->setText(QStringLiteral("%1").arg(speed));
    if (m_powerLabel)
        m_powerLabel->setText(QStringLiteral("%1").arg(power));
    if (m_avgSpeedLabel)
        m_avgSpeedLabel->setText(QStringLiteral("%1").arg(avg > 0 ? avg : 0));
    refreshFixedSkillTypeLabel();
    applyHitAdvice(hits, hitType, score, speed, power, durationMs);
    registerHitReplay(hits);
    if (m_classStudentMode) {
        m_aiScoreHint->setText(QStringLiteral("学员「%1」· 拍柄已记录 %2 次挥拍")
                                   .arg(m_currentSkill)
                                   .arg(hits));
    } else {
        const QString src = sourceHint.isEmpty() ? QStringLiteral("拍柄挥拍") : sourceHint;
        m_aiScoreHint->setText(QStringLiteral("%1 · 第 %2 次练习「%3」· 已记录 %4 次挥拍")
                                   .arg(src)
                                   .arg(m_practiceIndex)
                                   .arg(m_currentSkill)
                                   .arg(hits));
    }
}

void TrainingPage::onImuHitDetected(const QString &, double speedKmh, int powerTen, double, double, int durationMs,
                                    const QString &hitType, int strokeClassId, float strokeConf)
{
    if (!m_imuSubscribed)
        return;

    QString sourceKey = QStringLiteral("imu_rule");
    QString sourceHint = QStringLiteral("拍柄大幅波动");
    if (strokeConf >= 0.24f && hitType != QStringLiteral("挥拍")) {
        sourceKey = QStringLiteral("imu_cnn");
        sourceHint = QStringLiteral("拍柄 CNN · %1 %2%")
                         .arg(hitType)
                         .arg(static_cast<int>(strokeConf * 100.0f + 0.5f));
    } else if (hitType != QStringLiteral("挥拍")) {
        sourceHint = QStringLiteral("拍柄传感器 · %1").arg(hitType);
    }

    if (!tryAcceptPracticeHit(sourceKey))
        return;
    const int presetScore =
        m_classStudentMode ? classHitScoreFromImu(strokeClassId, hitType, strokeConf, qBound(1, powerTen, 10)) : -1;
    onImuHit(speedKmh, powerTen, presetScore, hitType, durationMs, sourceHint);
}

bool TrainingPage::tryAcceptPracticeHit(const QString &source)
{
    Q_UNUSED(source);
    if (m_classStudentMode)
        return true;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastPracticeHitMs > 0 && (now - m_lastPracticeHitMs) < practiceHitCooldownMs()) {
        return false;
    }
    m_lastPracticeHitMs = now;
    return true;
}

void TrainingPage::subscribeImu()
{
    if (m_classStudentMode) {
        m_imuSubscribed = true;
        m_cameraHitSubscribed = false;
        return;
    }
    /* 单人练习：摄像头高置信 / 拍柄大幅波动 / CNN 三路 OR，统一去重 */
    m_cameraHitSubscribed = true;
    m_imuSubscribed = true;
}

void TrainingPage::unsubscribeImu()
{
    m_imuSubscribed = false;
    m_cameraHitSubscribed = false;
}

void TrainingPage::enableCameraHitListen(bool on)
{
    if (m_classStudentMode) {
        m_cameraHitSubscribed = false;
        return;
    }
    m_cameraHitSubscribed = on;
    m_imuSubscribed = on;
}

void TrainingPage::stopTraining()
{
    unsubscribeImu();
    m_classStudentMode = false;
    m_classDeviceCode.clear();
    m_classStudentName.clear();
    m_classDeviceId = 0;
    applyTrainLayout();
}

void TrainingPage::resumeImu()
{
    subscribeImu();
}

void TrainingPage::startClassStudentTraining(const QString &studentName, const QString &deviceCode)
{
    m_classStudentMode = true;
    m_classStudentName = studentName;
    m_classDeviceCode = deviceCode;
    bool ok = false;
    const int id = deviceCode.toInt(&ok);
    m_classDeviceId = (ok && id > 0) ? id : 0;

    m_currentSkill = studentName;
    m_practiceIndex = 1;
    m_titleLabel->setText(QStringLiteral("学员训练 · %1").arg(studentName));
    m_scores.clear();
    m_speedsKmh.clear();
    m_powersTen.clear();
    m_hitTypes.clear();
    m_durationsMs.clear();
    m_speedSum = 0;
    m_speedCount = 0;
    m_powerSum = 0;
    m_powerCount = 0;

    m_aiScoreValue->setText(QStringLiteral("--"));
    m_aiScoreHint->setText(QStringLiteral("设备 %1 · 挥拍后实时更新指标").arg(deviceCode));
    m_correctionText->setText(QStringLiteral("由拍柄 IMU 数据变化识别挥拍并统计。"));
    if (m_hitCountLabel)
        m_hitCountLabel->setText(QStringLiteral("0"));
    if (m_speedLabel)
        m_speedLabel->setText(QStringLiteral("--"));
    if (m_powerLabel)
        m_powerLabel->setText(QStringLiteral("--"));
    if (m_avgSpeedLabel)
        m_avgSpeedLabel->setText(QStringLiteral("--"));
    if (m_actionTypeLabel)
        m_actionTypeLabel->setText(QStringLiteral("--"));

    applyTrainLayout();
    if (m_mainWindow && m_mainWindow->sleImu()) {
        m_mainWindow->sleImu()->resetSwingDetector();
        m_mainWindow->sleImu()->start();
    }
    subscribeImu();
}

void TrainingPage::startTraining(const QString &skill, int practiceIndex) {
    m_classStudentMode = false;
    m_classDeviceCode.clear();
    m_classStudentName.clear();
    m_classDeviceId = 0;
    applyTrainLayout();

    m_currentSkill = skill;
    m_practiceIndex = practiceIndex;
    m_titleLabel->setText("训练中 · " + skill);
    m_scores.clear();
    m_speedsKmh.clear();
    m_powersTen.clear();
    m_replaySessionId = QString::number(QDateTime::currentMSecsSinceEpoch());
    publishReplaySession(m_replaySessionId);
    QDir().mkpath(replaySessionDir(m_replaySessionId));
    m_speedSum = 0;
    m_speedCount = 0;
    m_powerSum = 0;
    m_powerCount = 0;

    m_aiScoreValue->setText(QStringLiteral("--"));
    m_aiScoreHint->setText(QString("这是你第 %1 次练习「%2」").arg(practiceIndex).arg(skill));
    m_correctionText->setText(QStringLiteral("击球：摄像头高置信 / 拍柄大幅波动 / CNN 九轴，满足任一即计数。"));
    if (m_hitCountLabel)
        m_hitCountLabel->setText(QStringLiteral("0"));
    if (m_speedLabel)
        m_speedLabel->setText(QStringLiteral("--"));
    if (m_powerLabel)
        m_powerLabel->setText(QStringLiteral("--"));
    if (m_avgSpeedLabel)
        m_avgSpeedLabel->setText(QStringLiteral("--"));
    refreshFixedSkillTypeLabel();

    if (m_mainWindow && m_mainWindow->sleImu())
        m_mainWindow->sleImu()->resetSwingDetector();
    if (m_mainWindow && m_mainWindow->yoloAction())
        m_mainWindow->yoloAction()->resetSessionBaseline();
    m_lastPracticeHitMs = 0;
    m_prevYoloStable = false;
    m_prevYoloCls = -1;
    m_prevYoloScore = 0.0f;
    subscribeImu();
}

// ═══════════════════════════════════════════════
// 训练总结 实现
// ═══════════════════════════════════════════════
TrainingSummaryPage::TrainingSummaryPage(MainWindow *mw, QWidget *parent)
    : PageBase("本次训练总结", "", mw, parent), m_source("training")
{
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();

    auto *wrap = new QVBoxLayout();
    wrap->setContentsMargins(24, 24, 24, 24);
    wrap->setSpacing(16);

    auto *head = new QFrame();
    head->setObjectName("card");
    head->setStyleSheet("QFrame#card { border: 1px solid #234f8c; border-radius: 16px; background: #0a1a33c7; }");
    auto *hdLay = new QHBoxLayout(head);
    hdLay->setContentsMargins(16, 14, 16, 14);
    auto *hdLeft = new QVBoxLayout();
    m_summaryTitle = new QLabel("第 - 次练习 · -");
    m_summaryTitle->setStyleSheet("font-size:18px; font-weight:800; background:transparent; color:#eaf3ff;");
    hdLeft->addWidget(m_summaryTitle);
    m_summarySub = new QLabel("");
    m_summarySub->setStyleSheet("font-size:13px; color:#9eb7de; background:transparent;");
    hdLeft->addWidget(m_summarySub);
    hdLay->addLayout(hdLeft);
    hdLay->addStretch();
    m_avgBadge = new QLabel("平均 -- 分");
    m_avgBadge->setStyleSheet("font-size: 14px; font-weight: 700; color: #cfe2ff; border: 1px solid #2b5aa0; border-radius: 999px; padding: 6px 14px; background: #07172f7a;");
    hdLay->addWidget(m_avgBadge);
    wrap->addWidget(head);

    // 得分方块网格
    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    auto *gridWidget = new QWidget();
    gridWidget->setStyleSheet("background: transparent;");
    m_scoreGrid = new QGridLayout(gridWidget);
    m_scoreGrid->setSpacing(12);
    m_scoreGrid->setColumnStretch(0, 1);
    scrollArea->setWidget(gridWidget);
    wrap->addWidget(scrollArea, 1);

    m_rootLayout->addLayout(wrap);
}

void TrainingSummaryPage::showSummary(const QString &skill, int practiceIndex, const QList<int> &scores,
                                      const QList<int> &speedsKmh, const QList<int> &powersTen) {
    m_source = "training";
    MainWindow *mw = m_mainWindow;
    connect(m_backBtn, &QPushButton::clicked, this, [this, mw]() {
        if (mw && mw->m_training)
            mw->m_training->resumeImu();
        if (mw)
            mw->switchPage(4);
    }, Qt::UniqueConnection);
    m_backBtn->setText("返回训练中");

    m_summaryTitle->setText(QString("第 %1 次练习 · %2").arg(practiceIndex).arg(skill));
    const bool hasImu = speedsKmh.size() == scores.size() && !speedsKmh.isEmpty();
    const bool hasPower = powersTen.size() == scores.size() && !powersTen.isEmpty();
    m_summarySub->setText(hasImu
                              ? QString("本次共 %1 次挥拍 · 球速/力度来自拍柄 IMU").arg(scores.size())
                              : QString("本次共 %1 次动作得分（占位数据）").arg(scores.size()));

    int avg = scores.isEmpty() ? 0 : std::accumulate(scores.begin(), scores.end(), 0) / scores.size();
    m_avgBadge->setText(QString("平均 %1 分").arg(avg));

    // 清除旧网格
    QLayoutItem *child;
    while ((child = m_scoreGrid->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    for (int i = 0; i < scores.size(); ++i) {
        auto *card = new QPushButton();
        card->setFixedSize(140, 105);
        card->setCursor(Qt::PointingHandCursor);
        card->setStyleSheet(R"(
            QPushButton {
                border: 1px solid #2e63ac; border-radius: 14px;
                background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445);
                color: #eaf3ff;
            }
            QPushButton:hover { border-color: #56baff; }
        )");
        auto *cl = new QVBoxLayout(card);
        cl->setSpacing(4);
        auto *idxLabel = new QLabel(QString("第 %1 次").arg(i + 1));
        idxLabel->setStyleSheet("font-size:11px; color:#8cc7ff; background:transparent;");
        idxLabel->setAlignment(Qt::AlignCenter);
        cl->addWidget(idxLabel);
        auto *scoreLabel = new QLabel(QString("%1 分").arg(scores[i]));
        scoreLabel->setStyleSheet("font-size:20px; font-weight:900; background:transparent;");
        scoreLabel->setAlignment(Qt::AlignCenter);
        cl->addWidget(scoreLabel);
        if (hasImu && i < speedsKmh.size()) {
            auto *spdLabel = new QLabel(QString("%1 km/h").arg(speedsKmh[i]));
            spdLabel->setStyleSheet("font-size:12px; color:#8cc7ff; background:transparent;");
            spdLabel->setAlignment(Qt::AlignCenter);
            cl->addWidget(spdLabel);
        }
        if (hasPower && i < powersTen.size()) {
            auto *pwrLabel = new QLabel(QString("力度 %1/10").arg(powersTen[i]));
            pwrLabel->setStyleSheet("font-size:11px; color:#9eb7de; background:transparent;");
            pwrLabel->setAlignment(Qt::AlignCenter);
            cl->addWidget(pwrLabel);
        }
        int idx = i + 1, sc = scores[i];
        connect(card, &QPushButton::clicked, this, [this, idx, sc]() { emit actionClicked(idx, sc); });
        m_scoreGrid->addWidget(card, i / 6, i % 6);
    }
}

void TrainingSummaryPage::showClassStudentSummary(const QString &studentName, const QString &deviceCode,
                                                   int swings, const QList<int> &scores,
                                                   const QList<int> &speedsKmh,
                                                   const QList<int> &powersTen,
                                                   const QList<QString> &hitTypes,
                                                   const QList<int> &durationsMs) {
    m_source = "classTrain";
    m_classStudentName = studentName;
    connect(m_backBtn, &QPushButton::clicked, m_mainWindow, [this]() { m_mainWindow->switchPage(12); }, Qt::UniqueConnection);
    m_backBtn->setText("返回班级同训");

    m_summaryTitle->setText(studentName + QStringLiteral(" · 挥拍记录"));
    const bool hasImu = speedsKmh.size() == scores.size() && !speedsKmh.isEmpty();
    const bool hasPower = powersTen.size() == scores.size() && !powersTen.isEmpty();
    const bool hasType = hitTypes.size() == scores.size() && !hitTypes.isEmpty();
    const bool hasDuration = durationsMs.size() == scores.size() && !durationsMs.isEmpty();
    m_summarySub->setText(hasType
                              ? QStringLiteral("设备 %1 · 共 %2 次 · 点击某次查看动作详情（IMU 1D CNN）")
                                    .arg(deviceCode)
                                    .arg(swings)
                              : QStringLiteral("设备 %1 · 共 %2 次挥拍 · 点击查看详情").arg(deviceCode).arg(swings));

    int avg = scores.isEmpty() ? 0 : std::accumulate(scores.begin(), scores.end(), 0) / scores.size();
    m_avgBadge->setText(QString("平均 %1 分").arg(avg));

    QLayoutItem *child;
    while ((child = m_scoreGrid->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    if (scores.isEmpty()) {
        auto *emptyLabel = new QLabel("该学员暂无挥拍记录");
        emptyLabel->setStyleSheet("color:#556; font-size:16px; background:transparent;");
        m_scoreGrid->addWidget(emptyLabel, 0, 0);
        return;
    }

    for (int i = 0; i < scores.size(); ++i) {
        auto *row = new QPushButton();
        row->setCursor(Qt::PointingHandCursor);
        row->setMinimumHeight(120);
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        const QString typeText = (hasType && i < hitTypes.size() && !hitTypes[i].trimmed().isEmpty())
                                     ? hitTypes[i].trimmed()
                                     : QStringLiteral("挥拍");
        row->setStyleSheet(R"(
            QPushButton {
                border: 1px solid #2e63ac; border-radius: 18px;
                background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445);
                color: #eaf3ff; text-align: left; padding: 14px 18px;
            }
            QPushButton:hover { border-color: #56baff; }
        )");
        auto *rl = new QHBoxLayout(row);
        rl->setSpacing(18);
        rl->setContentsMargins(6, 6, 6, 6);
        auto *idxLabel = new QLabel(QStringLiteral("第 %1 次").arg(i + 1));
        idxLabel->setFixedWidth(88);
        idxLabel->setStyleSheet("font-size:22px; font-weight:900; color:#8cc7ff; background:transparent;");
        idxLabel->setAlignment(Qt::AlignCenter);
        rl->addWidget(idxLabel);
        auto *typeLabel = new QLabel(typeText);
        typeLabel->setStyleSheet("font-size:22px; font-weight:800; color:#eaf3ff; background:transparent;");
        typeLabel->setMinimumWidth(120);
        rl->addWidget(typeLabel, 1);
        auto *scoreLabel = new QLabel(QStringLiteral("%1 分").arg(scores[i]));
        scoreLabel->setMinimumWidth(88);
        scoreLabel->setStyleSheet("font-size:30px; font-weight:900; color:#ffcf66; background:transparent;");
        scoreLabel->setAlignment(Qt::AlignCenter);
        rl->addWidget(scoreLabel);
        if (hasImu && i < speedsKmh.size()) {
            auto *spdLabel = new QLabel(QStringLiteral("%1 km/h").arg(speedsKmh[i]));
            spdLabel->setMinimumWidth(108);
            spdLabel->setStyleSheet("font-size:19px; font-weight:700; color:#8cc7ff; background:transparent; "
                                    "border:1px solid #2e63ac; border-radius:14px; padding:10px 12px;");
            spdLabel->setAlignment(Qt::AlignCenter);
            rl->addWidget(spdLabel);
        }
        if (hasPower && i < powersTen.size()) {
            auto *pwrLabel = new QLabel(QStringLiteral("力度 %1/10").arg(powersTen[i]));
            pwrLabel->setMinimumWidth(116);
            pwrLabel->setStyleSheet("font-size:19px; font-weight:700; color:#cfe2ff; background:transparent; "
                                    "border:1px solid #2e63ac; border-radius:14px; padding:10px 12px;");
            pwrLabel->setAlignment(Qt::AlignCenter);
            rl->addWidget(pwrLabel);
        }
        const int idx = i + 1;
        const int sc = scores[i];
        const QString ht = typeText;
        const int spd = (hasImu && i < speedsKmh.size()) ? speedsKmh[i] : -1;
        const int pwr = (hasPower && i < powersTen.size()) ? powersTen[i] : -1;
        const int dur = (hasDuration && i < durationsMs.size()) ? durationsMs[i] : -1;
        connect(row, &QPushButton::clicked, this, [this, idx, sc, ht, spd, pwr, dur]() {
            emit classHitClicked(idx, sc, ht, spd, pwr, dur);
        });
        m_scoreGrid->addWidget(row, i, 0);
    }
}

// ═══════════════════════════════════════════════
// 班级单次动作详情 实现
// ═══════════════════════════════════════════════
ClassHitDetailPage::ClassHitDetailPage(MainWindow *mw, QWidget *parent)
    : PageBase(QStringLiteral("学员挥拍详情"), "", mw, parent, PageHeaderMode::SingleCentered)
{
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();

    static const char *kPanelStyle =
        "QFrame#classHitPanel { border: 1px solid #234f8c; border-radius: 16px; "
        "background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(10,26,51,199), stop:1 rgba(8,22,45,209)); }";

    auto makeMetricChip = [](const QString &title, QLabel *&value, const QString &unitHint = QString()) -> QFrame * {
        auto *f = new QFrame();
        f->setObjectName(QStringLiteral("classHitMetricChip"));
        f->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        f->setMinimumHeight(168);
        f->setStyleSheet(
            "QFrame#classHitMetricChip { border: 1px solid #2e63ac; border-radius: 14px; "
            "background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(12,32,62,220), stop:1 rgba(7,23,47,200)); }");
        auto *fl = new QVBoxLayout(f);
        fl->setContentsMargins(20, 18, 20, 18);
        fl->setSpacing(8);
        auto *tl = new QLabel(title);
        tl->setStyleSheet(QStringLiteral("font-size:20px; color:#8cc7ff; font-weight:700; background:transparent;"));
        fl->addWidget(tl);
        value = new QLabel(QStringLiteral("--"));
        value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        value->setStyleSheet(QStringLiteral("font-size:48px; font-weight:900; color:#eaf3ff; background:transparent;"));
        fl->addWidget(value, 1);
        if (!unitHint.isEmpty()) {
            auto *ul = new QLabel(unitHint);
            ul->setStyleSheet(QStringLiteral("font-size:28px; color:#b9cff1; font-weight:700; background:transparent;"));
            fl->addWidget(ul);
        }
        return f;
    };

    auto *body = new QVBoxLayout();
    body->setContentsMargins(12, 4, 12, 10);
    body->setSpacing(14);

    m_indexLabel = new QLabel(QStringLiteral("第 - 次挥拍"));
    m_indexLabel->setAlignment(Qt::AlignCenter);
    m_indexLabel->setStyleSheet(
        QStringLiteral("font-size:24px; color:#9eb7de; font-weight:700; letter-spacing:1px; background:transparent;"));
    body->addWidget(m_indexLabel);

    auto *mainPanel = new QFrame();
    mainPanel->setObjectName(QStringLiteral("classHitPanel"));
    mainPanel->setStyleSheet(kPanelStyle);
    mainPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mainPanel->setMinimumHeight(520);
    auto *mainLay = new QGridLayout(mainPanel);
    mainLay->setContentsMargins(22, 22, 22, 22);
    mainLay->setHorizontalSpacing(20);
    mainLay->setVerticalSpacing(16);
    mainLay->setColumnStretch(0, 11);
    mainLay->setColumnStretch(1, 13);
    mainLay->setRowStretch(0, 5);
    mainLay->setRowStretch(1, 4);

    auto *typeCard = new QFrame();
    typeCard->setObjectName(QStringLiteral("classHitTypeCard"));
    typeCard->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    typeCard->setMinimumHeight(420);
    typeCard->setStyleSheet(
        "QFrame#classHitTypeCard { border: 1px solid #3a6bb8; border-radius: 14px; "
        "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 rgba(18,48,88,230), stop:1 rgba(10,28,56,230)); }");
    auto *typeLay = new QVBoxLayout(typeCard);
    typeLay->setContentsMargins(28, 28, 28, 28);
    typeLay->setSpacing(14);
    auto *typeTitle = new QLabel(QStringLiteral("识别动作"));
    typeTitle->setStyleSheet(QStringLiteral("font-size:22px; color:#8cc7ff; font-weight:700; background:transparent;"));
    typeLay->addWidget(typeTitle);
    typeLay->addStretch(1);
    m_hitTypeValue = new QLabel(QStringLiteral("—"));
    m_hitTypeValue->setAlignment(Qt::AlignCenter);
    m_hitTypeValue->setWordWrap(true);
    m_hitTypeValue->setStyleSheet(
        QStringLiteral("font-size:88px; font-weight:900; color:#ffcf66; background:transparent; padding:12px 0;"));
    typeLay->addWidget(m_hitTypeValue, 0);
    typeLay->addStretch(1);
    auto *typeSub = new QLabel(QStringLiteral("拍柄 IMU + 1D CNN"));
    typeSub->setAlignment(Qt::AlignCenter);
    typeSub->setStyleSheet(QStringLiteral("font-size:18px; color:#9eb7de; background:transparent;"));
    typeLay->addWidget(typeSub);
    mainLay->addWidget(typeCard, 0, 0, 2, 1);

    auto *scorePanel = new QFrame();
    scorePanel->setObjectName(QStringLiteral("classHitMetricChip"));
    scorePanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scorePanel->setMinimumHeight(200);
    scorePanel->setStyleSheet(
        "QFrame#classHitMetricChip { border: 1px solid #2e63ac; border-radius: 14px; "
        "background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(12,32,62,220), stop:1 rgba(7,23,47,200)); }");
    auto *scoreLay = new QVBoxLayout(scorePanel);
    scoreLay->setContentsMargins(24, 20, 24, 20);
    scoreLay->setSpacing(10);
    auto *scoreTitle = new QLabel(QStringLiteral("综合得分"));
    scoreTitle->setStyleSheet(QStringLiteral("font-size:22px; color:#8cc7ff; font-weight:700; background:transparent;"));
    scoreLay->addWidget(scoreTitle);
    auto *scoreRow = new QHBoxLayout();
    scoreRow->setSpacing(10);
    m_scoreValue = new QLabel(QStringLiteral("--"));
    m_scoreValue->setStyleSheet(
        QStringLiteral("font-size:88px; font-weight:900; line-height:0.95; color:#eaf3ff; background:transparent;"));
    auto *scoreUnit = new QLabel(QStringLiteral("分"));
    scoreUnit->setStyleSheet(QStringLiteral("color:#b9cff1; font-size:30px; font-weight:700; background:transparent;"));
    scoreRow->addWidget(m_scoreValue, 0, Qt::AlignBottom);
    scoreRow->addWidget(scoreUnit, 0, Qt::AlignBottom);
    scoreRow->addStretch();
    scoreLay->addLayout(scoreRow);
    scoreLay->addStretch();
    mainLay->addWidget(scorePanel, 0, 1);

    auto *metricsRow = new QHBoxLayout();
    metricsRow->setSpacing(16);
    metricsRow->addWidget(makeMetricChip(QStringLiteral("球速"), m_speedValue, QStringLiteral("km/h")), 1);
    metricsRow->addWidget(makeMetricChip(QStringLiteral("击球力度"), m_powerValue, QStringLiteral("/10")), 1);
    metricsRow->addWidget(makeMetricChip(QStringLiteral("挥拍时长"), m_durationValue, QStringLiteral("秒")), 1);
    mainLay->addLayout(metricsRow, 1, 1);

    body->addWidget(mainPanel, 12);

    auto *hintPanel = new QFrame();
    hintPanel->setObjectName(QStringLiteral("classHitPanel"));
    hintPanel->setStyleSheet(kPanelStyle);
    hintPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    hintPanel->setMinimumHeight(120);
    auto *hintLay = new QVBoxLayout(hintPanel);
    hintLay->setContentsMargins(24, 20, 24, 20);
    hintLay->setSpacing(10);
    auto *hintTitleRow = new QHBoxLayout();
    hintTitleRow->setSpacing(10);
    auto *hintTitle = new QLabel(QStringLiteral("AI 建议"));
    hintTitle->setStyleSheet(QStringLiteral("font-size:20px; color:#8cc7ff; font-weight:700; background:transparent;"));
    hintTitleRow->addWidget(hintTitle);
    auto *aiBadge = new QLabel(QStringLiteral("智能教练"));
    aiBadge->setStyleSheet(
        QStringLiteral("font-size:14px; color:#eaf3ff; font-weight:700; background:#1a4a8a; "
                       "border:1px solid #3a7fd4; border-radius:10px; padding:4px 10px;"));
    hintTitleRow->addWidget(aiBadge);
    hintTitleRow->addStretch();
    hintLay->addLayout(hintTitleRow);
    m_hintText = new QLabel();
    m_hintText->setWordWrap(true);
    m_hintText->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_hintText->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_hintText->setStyleSheet(QStringLiteral("font-size:30px; color:#d9e9ff; line-height:1.55; background:transparent;"));
    hintLay->addWidget(m_hintText, 1);
    body->addWidget(hintPanel, 4);

    auto *backBtn = new QPushButton(QStringLiteral("返回挥拍记录"));
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setFixedHeight(80);
    backBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    backBtn->setStyleSheet(R"(
        QPushButton {
            border: 1px solid #2f6ab4; border-radius: 16px;
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #153a6f, stop:1 #0f2d56);
            color: #eaf3ff; font-size: 22px; font-weight: 700;
        }
        QPushButton:hover { border-color: #56baff; }
    )");
    connect(backBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(5); });
    body->addWidget(backBtn, 0);

    m_rootLayout->addLayout(body, 1);
}

void ClassHitDetailPage::showHit(int idx, const QString &studentName, const QString &hitType, int score,
                                 int speedKmh, int powerTen, int durationMs)
{
    connect(m_backBtn, &QPushButton::clicked, m_mainWindow, [this]() { m_mainWindow->switchPage(5); },
            Qt::UniqueConnection);
    m_backBtn->setText(QStringLiteral("返回挥拍记录"));

    const QString typeLabel = hitType.trimmed().isEmpty() ? QStringLiteral("挥拍") : hitType.trimmed();
    const QString durationText =
        durationMs >= 0 ? QString::number(durationMs / 1000.0, 'f', 2) : QStringLiteral("--");
    m_titleLabel->setText(studentName + QStringLiteral(" · 第 %1 次").arg(idx));
    m_indexLabel->setText(QStringLiteral("第 %1 次挥拍 · %2").arg(idx).arg(studentName));
    m_hitTypeValue->setText(typeLabel);
    m_scoreValue->setText(QString::number(score));
    m_speedValue->setText(speedKmh >= 0 ? QString::number(speedKmh) : QStringLiteral("--"));
    m_powerValue->setText(powerTen >= 0 ? QString::number(powerTen) : QStringLiteral("--"));
    m_durationValue->setText(durationText);

    ClassHitAdviceContext adviceCtx;
    adviceCtx.studentName = studentName;
    adviceCtx.hitIdx = idx;
    adviceCtx.hitType = typeLabel;
    adviceCtx.score = score;
    adviceCtx.speedKmh = speedKmh;
    adviceCtx.powerTen = powerTen;
    adviceCtx.durationMs = durationMs;
    m_hintText->setText(pickClassHitAiAdvice(adviceCtx));
}

// ═══════════════════════════════════════════════
// 动作详情 实现（对齐 index.html #pageActionDetail）
// ═══════════════════════════════════════════════
static QString actionDetailFormatSpeedRate(double rate)
{
    return formatPlaybackSpeedRate(rate);
}

void ActionDetailPage::highlightSpeedButton(QPushButton *active, double rate)
{
    m_playbackRate = rate;
    for (auto *b : m_speedButtons)
        b->setStyleSheet(QLatin1String(VIDEO_SPEED_BTN_NORMAL));
    if (active)
        active->setStyleSheet(QLatin1String(VIDEO_SPEED_BTN_ACTIVE));
    if (m_speedValueText)
        m_speedValueText->setText(QStringLiteral("%1x").arg(actionDetailFormatSpeedRate(rate)));
    applyPlaybackRate();
}

void ActionDetailPage::applyPlaybackRate()
{
    if (m_replayPlayer && m_videoStack && m_videoStack->currentWidget() == m_replayVideoPage)
        m_replayPlayer->setPlaybackRate(static_cast<qreal>(m_playbackRate));
    if (m_frameReplay && m_videoStack && m_videoStack->currentWidget() == m_frameReplay) {
        m_frameReplay->setPlaybackRate(m_playbackRate);
        m_frameReplay->play();
    }
}

void ActionDetailPage::stopReplay()
{
    if (m_replayWaitTimer)
        m_replayWaitTimer->stop();
    if (m_replayPlayer) {
        m_replayPlayer->stop();
        m_replayPlayer->setMedia(QMediaContent());
    }
    if (m_frameReplay)
        m_frameReplay->stopPlayback();
}

void ActionDetailPage::playReplayClip(const QString &clipPath)
{
    stopReplay();
    if (clipPath.isEmpty()) {
        if (m_videoStack && m_replayPlaceholder) {
            m_videoStack->setCurrentWidget(m_replayPlaceholder);
            m_replayPlaceholder->setText(QStringLiteral("暂无击球回放\n\n请完成一次单人练习并击球后再查看。"));
        }
        return;
    }

    const QFileInfo fi(clipPath);
    if (fi.isFile() && fi.suffix().compare(QStringLiteral("mp4"), Qt::CaseInsensitive) == 0) {
        if (m_videoStack && m_replayVideoPage) {
            m_videoStack->setCurrentWidget(m_replayVideoPage);
            m_replayPlayer->setMedia(QMediaContent(QUrl::fromLocalFile(clipPath)));
            m_replayPlayer->setPlaybackRate(static_cast<qreal>(m_playbackRate));
            m_replayPlayer->play();
        }
        return;
    }

    if (fi.isDir() && m_frameReplay && m_videoStack) {
        m_frameReplay->loadFromDir(clipPath);
        m_videoStack->setCurrentWidget(m_frameReplay);
        m_frameReplay->setPlaybackRate(m_playbackRate);
        m_frameReplay->play();
        return;
    }

    if (m_videoStack && m_replayPlaceholder) {
        m_videoStack->setCurrentWidget(m_replayPlaceholder);
        m_replayPlaceholder->setText(QStringLiteral("回放文件无效：\n%1").arg(clipPath));
    }
}

void ActionDetailPage::tryStartReplay()
{
    const QString clip = resolveReplayClip(m_pendingReplaySession, m_pendingHitIdx);
    if (!clip.isEmpty()) {
        m_replayWaitTicks = 0;
        if (m_replayWaitTimer)
            m_replayWaitTimer->stop();
        playReplayClip(clip);
        return;
    }
    m_replayWaitTicks++;
    if (m_replayWaitTicks > 225) {
        if (m_replayWaitTimer)
            m_replayWaitTimer->stop();
        if (m_videoStack && m_replayPlaceholder) {
            m_videoStack->setCurrentWidget(m_replayPlaceholder);
            m_replayPlaceholder->setText(QStringLiteral("回放生成超时\n\n请返回重新练习，或稍后再试。"));
        }
        return;
    }
    if (m_videoStack && m_replayPlaceholder) {
        m_videoStack->setCurrentWidget(m_replayPlaceholder);
        if (replayHitNeedsPoseRender(m_pendingReplaySession, m_pendingHitIdx)) {
            m_replayPlaceholder->setText(QStringLiteral("正在生成击球回放（骨骼渲染中）…"));
        } else {
            m_replayPlaceholder->setText(QStringLiteral("正在生成击球回放…"));
        }
    }
    if (m_replayWaitTimer == nullptr) {
        m_replayWaitTimer = new QTimer(this);
        m_replayWaitTimer->setInterval(400);
        connect(m_replayWaitTimer, &QTimer::timeout, this, &ActionDetailPage::tryStartReplay);
    }
    if (!m_replayWaitTimer->isActive())
        m_replayWaitTimer->start();
}

void ActionDetailPage::onReplayPlayerError(QMediaPlayer::Error)
{
    if (m_replayPlayer == nullptr)
        return;
    const QString dir = replayHitFramesDir(m_pendingReplaySession, m_pendingHitIdx);
    if (QFileInfo::exists(dir)) {
        playReplayClip(dir);
        return;
    }
    if (m_replayPlaceholder) {
        m_videoStack->setCurrentWidget(m_replayPlaceholder);
        m_replayPlaceholder->setText(QStringLiteral("MP4 播放失败，请稍后重试。\n%1").arg(m_replayPlayer->errorString()));
    }
}

void ActionDetailPage::hideEvent(QHideEvent *event)
{
    stopReplay();
    PageBase::hideEvent(event);
}

ActionDetailPage::ActionDetailPage(MainWindow *mw, QWidget *parent)
    : PageBase("动作详情", "", mw, parent, PageHeaderMode::SingleCentered), m_source("training")
{
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();

    static const char *kPanelStyle =
        "QFrame#trainPanel { border: 1px solid #234f8c; border-radius: 16px; "
        "background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(10,26,51,199), stop:1 rgba(8,22,45,209)); }";

    auto makeTrainMetricChip = [](const QString &title, QLabel *&value, const QString &unitHint = QString()) -> QFrame * {
        auto *f = new QFrame();
        f->setObjectName("trainMetricChip");
        f->setMinimumHeight(96);
        f->setStyleSheet(
            "QFrame#trainMetricChip { border: 1px solid #2e63ac; border-radius: 14px; background: rgba(7,23,47,200); }");
        auto *fl = new QVBoxLayout(f);
        fl->setContentsMargins(14, 12, 14, 12);
        fl->setSpacing(4);
        auto *tl = new QLabel(title);
        tl->setStyleSheet("font-size:14px; color:#8cc7ff; font-weight:700; background:transparent;");
        fl->addWidget(tl);
        value = new QLabel(QStringLiteral("--"));
        value->setStyleSheet("font-size:28px; font-weight:900; color:#eaf3ff; background:transparent;");
        fl->addWidget(value);
        if (!unitHint.isEmpty()) {
            auto *ul = new QLabel(unitHint);
            ul->setStyleSheet("font-size:13px; color:#9eb7de; background:transparent;");
            fl->addWidget(ul);
        }
        return f;
    };

    auto *grid = new QGridLayout();
    grid->setContentsMargins(0, 4, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(0);
    grid->setColumnStretch(0, 0);
    grid->setColumnStretch(1, 1);

    // ── 左列：与训练页相同结构（打分 + 指标 + 纠正 + 返回按钮）
    auto *leftCol = new QWidget();
    leftCol->setMinimumWidth(kTrainLeftColW);
    leftCol->setMaximumWidth(kTrainLeftColW + 48);
    auto *lv = new QVBoxLayout(leftCol);
    lv->setContentsMargins(0, 36, 0, 4);
    lv->setSpacing(12);
    lv->addStretch(2);

    auto *scorePanel = new QFrame();
    scorePanel->setObjectName("trainPanel");
    scorePanel->setMinimumHeight(360);
    scorePanel->setStyleSheet(kPanelStyle);
    auto *spLay = new QVBoxLayout(scorePanel);
    spLay->setContentsMargins(16, 16, 16, 16);
    spLay->setSpacing(12);

    auto *kpiRow = new QGridLayout();
    kpiRow->setContentsMargins(0, 0, 0, 0);
    kpiRow->setHorizontalSpacing(12);
    kpiRow->setColumnStretch(0, 5);
    kpiRow->setColumnStretch(1, 6);

    auto *kpiCard = new QFrame();
    kpiCard->setObjectName("trainKpiCard");
    kpiCard->setMinimumHeight(200);
    kpiCard->setStyleSheet(
        "QFrame#trainKpiCard { border: 1px solid #2e63ac; border-radius: 14px; background: rgba(7,23,47,210); }");
    auto *kpiLay = new QVBoxLayout(kpiCard);
    kpiLay->setContentsMargins(16, 14, 16, 14);
    kpiLay->setSpacing(6);
    auto *spTitle = new QLabel(QStringLiteral("AI 打分"));
    spTitle->setStyleSheet("color:#8cc7ff; font-size:18px; font-weight:700; background:transparent;");
    kpiLay->addWidget(spTitle);
    m_indexText = new QLabel(QStringLiteral("第 - 次"));
    m_indexText->setStyleSheet("color:#9eb7de; font-size:15px; font-weight:700; background:transparent;");
    kpiLay->addWidget(m_indexText);

    auto *scoreRow = new QHBoxLayout();
    scoreRow->setSpacing(8);
    m_scoreValue = new QLabel(QStringLiteral("--"));
    m_scoreValue->setStyleSheet("font-size:80px; font-weight:900; line-height:0.95; color:#eaf3ff; background:transparent;");
    auto *scoreUnit = new QLabel(QStringLiteral("分"));
    scoreUnit->setStyleSheet("color:#b9cff1; font-size:26px; font-weight:700; background:transparent;");
    scoreRow->addWidget(m_scoreValue, 0, Qt::AlignBottom);
    scoreRow->addWidget(scoreUnit, 0, Qt::AlignBottom);
    scoreRow->addStretch();
    kpiLay->addLayout(scoreRow);
    kpiLay->addStretch();

    auto *metricsWrap = new QWidget();
    auto *metricsGrid = new QGridLayout(metricsWrap);
    metricsGrid->setContentsMargins(0, 0, 0, 0);
    metricsGrid->setSpacing(10);
    metricsGrid->addWidget(makeTrainMetricChip(QStringLiteral("球速"), m_metricSpeed, QStringLiteral("km/h")), 0, 0);
    metricsGrid->addWidget(makeTrainMetricChip(QStringLiteral("击球力度"), m_metricPower, QStringLiteral("/10")), 0, 1);
    metricsGrid->addWidget(makeTrainMetricChip(QStringLiteral("击球时机"), m_metricTiming), 1, 0);
    metricsGrid->addWidget(makeTrainMetricChip(QStringLiteral("挥拍幅度"), m_metricSwing), 1, 1);

    kpiRow->addWidget(kpiCard, 0, 0, Qt::AlignTop);
    kpiRow->addWidget(metricsWrap, 0, 1, Qt::AlignTop);
    spLay->addLayout(kpiRow);

    auto *scoreHint = new QLabel(QStringLiteral("查看本次击球数据与右侧回放。"));
    scoreHint->setStyleSheet("color:#cfe2ff; font-size:16px; line-height:1.45; background:transparent;");
    scoreHint->setWordWrap(true);
    spLay->addWidget(scoreHint);

    auto *corrPanel = new QFrame();
    corrPanel->setObjectName("trainPanel");
    corrPanel->setMinimumHeight(280);
    corrPanel->setStyleSheet(kPanelStyle);
    auto *cpLay = new QVBoxLayout(corrPanel);
    cpLay->setContentsMargins(18, 16, 18, 16);
    cpLay->setSpacing(10);
    auto *cpTitle = new QLabel(QStringLiteral("动作纠正"));
    cpTitle->setStyleSheet("color:#8cc7ff; font-size:23px; font-weight:700; background:transparent;");
    cpLay->addWidget(cpTitle);
    m_aiImprove = new QLabel(QStringLiteral("—"));
    m_aiImprove->setStyleSheet("color:#d9e9ff; font-size:22px; line-height:1.55; background:transparent;");
    m_aiImprove->setWordWrap(true);
    cpLay->addWidget(m_aiImprove, 1);

    auto *commentPanel = new QFrame();
    commentPanel->setObjectName("trainPanel");
    commentPanel->setMinimumHeight(200);
    commentPanel->setStyleSheet(kPanelStyle);
    auto *cmLay = new QVBoxLayout(commentPanel);
    cmLay->setContentsMargins(18, 16, 18, 16);
    cmLay->setSpacing(10);
    auto *cmTitle = new QLabel(QStringLiteral("AI 评价"));
    cmTitle->setStyleSheet("color:#8cc7ff; font-size:23px; font-weight:700; background:transparent;");
    cmLay->addWidget(cmTitle);
    m_aiComment = new QLabel(QStringLiteral("—"));
    m_aiComment->setStyleSheet("color:#d9e9ff; font-size:22px; line-height:1.55; background:transparent;");
    m_aiComment->setWordWrap(true);
    cmLay->addWidget(m_aiComment, 1);

    lv->addWidget(scorePanel, 5);
    lv->addWidget(commentPanel, 3);
    lv->addWidget(corrPanel, 4);

    auto *backBtnPanel = new QWidget();
    auto *backBtnLay = new QVBoxLayout(backBtnPanel);
    backBtnLay->setContentsMargins(0, 4, 0, 0);
    auto *summaryBtn = new QPushButton(QStringLiteral("返回总结"));
    summaryBtn->setCursor(Qt::PointingHandCursor);
    summaryBtn->setFixedHeight(76);
    summaryBtn->setStyleSheet(R"(
        QPushButton {
            border: 1px solid #2f6ab4; border-radius: 16px;
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #153a6f, stop:1 #0f2d56);
            color: #eaf3ff; font-size: 19px; font-weight: 700;
        }
        QPushButton:hover { border-color: #56baff; }
    )");
    connect(summaryBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(5); });
    backBtnLay->addWidget(summaryBtn);
    lv->addWidget(backBtnPanel, 0);

    // ── 右列：回放区 + 倍速条（对齐训练页摄像头尺寸与靠右布局）
    auto *rightCol = new QWidget();
    auto *rv = new QVBoxLayout(rightCol);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(12);
    rv->addStretch(2);

    auto *replayPanel = new QFrame();
    replayPanel->setObjectName("trainCam");
    replayPanel->setFixedSize(kCamTrainingW, kCamTrainingH);
    replayPanel->setStyleSheet(
        "QFrame#trainCam { border: 1px solid #234f8c; border-radius: 16px; background: #000000; }");
    auto *rpLay = new QVBoxLayout(replayPanel);
    rpLay->setContentsMargins(0, 0, 0, 0);
    rpLay->setSpacing(0);

    m_videoStack = new QStackedWidget(replayPanel);
    m_videoStack->setFixedSize(kCamTrainingW, kCamTrainingH);

    m_replayVideo = new QVideoWidget();
    m_replayVideo->setMinimumSize(kCamTrainingW, kCamTrainingH);
    m_replayVideo->setAspectRatioMode(Qt::KeepAspectRatio);
    m_replayVideo->setStyleSheet(QStringLiteral("background:#000;"));
    m_replayVideoPage = new QWidget();
    auto *videoPageLay = new QVBoxLayout(m_replayVideoPage);
    videoPageLay->setContentsMargins(0, 0, 0, 0);
    videoPageLay->addWidget(m_replayVideo);

    m_frameReplay = new FrameReplayWidget();
    m_replayPlaceholder = new QLabel(QStringLiteral("击球回放加载中…"));
    m_replayPlaceholder->setAlignment(Qt::AlignCenter);
    m_replayPlaceholder->setWordWrap(true);
    m_replayPlaceholder->setStyleSheet(QStringLiteral("color:#b8d5ff;font-size:22px;background:transparent;"));

    m_videoStack->addWidget(m_replayVideoPage);
    m_videoStack->addWidget(m_frameReplay);
    m_videoStack->addWidget(m_replayPlaceholder);
    m_videoStack->setCurrentWidget(m_replayPlaceholder);
    rpLay->addWidget(m_videoStack, 1);

    m_replayPlayer = new QMediaPlayer(this, QMediaPlayer::VideoSurface);
    m_replayPlayer->setVideoOutput(m_replayVideo);
    m_replayPlayer->setMuted(true);
    connect(m_replayPlayer, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this,
            &ActionDetailPage::onReplayPlayerError);

    auto *speedFrame = new QFrame();
    speedFrame->setMinimumHeight(78);
    speedFrame->setStyleSheet(
        "QFrame { border: 1px solid #234f8c; border-radius: 16px; background: rgba(7,23,47,220); }");
    auto *speedLay = new QHBoxLayout(speedFrame);
    speedLay->setContentsMargins(18, 12, 18, 12);
    speedLay->setSpacing(16);

    auto *speedLeft = new QWidget();
    auto *slLay = new QHBoxLayout(speedLeft);
    slLay->setContentsMargins(0, 0, 0, 0);
    slLay->setSpacing(10);
    auto *speedPrefix = new QLabel(QStringLiteral("倍速"));
    speedPrefix->setStyleSheet("color:#9eb7de; font-size:22px; font-weight:700; background:transparent;");
    m_speedValueText = new QLabel(QStringLiteral("1.0x"));
    m_speedValueText->setStyleSheet("color:#eaf3ff; font-size:32px; font-weight:900; background:transparent;");
    slLay->addWidget(speedPrefix);
    slLay->addWidget(m_speedValueText);

    speedLay->addWidget(speedLeft);
    speedLay->addStretch(1);

    auto *speedActions = new QWidget();
    auto *saLay = new QHBoxLayout(speedActions);
    saLay->setContentsMargins(0, 0, 0, 0);
    saLay->setSpacing(12);

    struct RateBtn {
        double r;
        const char *label;
    };
    const RateBtn rates[] = {{0.5, "0.5x"}, {1.0, "1x"}, {1.5, "1.5x"}, {2.0, "2x"}};
    m_speedButtons.clear();
    QPushButton *defaultActive = nullptr;
    for (const auto &rb : rates) {
        auto *b = new QPushButton(QString::fromUtf8(rb.label));
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(56);
        b->setStyleSheet(QLatin1String(VIDEO_SPEED_BTN_NORMAL));
        m_speedButtons.append(b);
        saLay->addWidget(b);
        if (qFuzzyCompare(rb.r, 1.0))
            defaultActive = b;
        connect(b, &QPushButton::clicked, this, [this, b, rate = rb.r]() { highlightSpeedButton(b, rate); });
    }
    speedLay->addWidget(speedActions, 0, Qt::AlignRight);

    auto *replayCell = new QWidget();
    auto *replayCellLay = new QVBoxLayout(replayCell);
    replayCellLay->setContentsMargins(0, 0, 0, 0);
    replayCellLay->setSpacing(12);
    auto *replayRow = new QHBoxLayout();
    replayRow->setContentsMargins(0, 0, 0, 0);
    replayRow->addStretch(1);
    replayRow->addWidget(replayPanel, 0, Qt::AlignRight | Qt::AlignVCenter);
    replayCellLay->addLayout(replayRow);
    auto *speedRow = new QHBoxLayout();
    speedRow->setContentsMargins(0, 0, 0, 0);
    speedRow->addStretch(1);
    speedRow->addWidget(speedFrame, 0, Qt::AlignRight);
    speedFrame->setFixedWidth(kCamTrainingW);
    replayCellLay->addLayout(speedRow);

    rv->addWidget(replayCell, 0, Qt::AlignRight | Qt::AlignVCenter);
    rv->addStretch(1);

    if (defaultActive)
        highlightSpeedButton(defaultActive, 1.0);

    grid->addWidget(leftCol, 0, 0, Qt::AlignTop);
    grid->addWidget(rightCol, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
    m_rootLayout->addLayout(grid, 1);
}

void ActionDetailPage::showAction(int idx, int score, const QString &skillLabel, int speedKmh, int powerTen,
                                  const QString &replaySessionId, int durationMs, const QString &hitType)
{
    m_source = "training";
    m_pendingReplaySession = replaySessionId;
    m_pendingHitIdx = idx;
    connect(m_backBtn, &QPushButton::clicked, m_mainWindow, [this]() { m_mainWindow->switchPage(5); }, Qt::UniqueConnection);
    m_backBtn->setText(QStringLiteral("返回总结"));
    m_titleLabel->setText(skillLabel + QStringLiteral(" · 第 %1 次").arg(idx));
    m_indexText->setText(QStringLiteral("第 %1 次").arg(idx));
    m_scoreValue->setText(QString::number(score));

    int speed = speedKmh;
    if (speed < 0) {
        speed = static_cast<int>(randInt(90, 210) + (score - 70) * 0.8);
        speed = std::max(60, speed);
    }
    int power = powerTen;
    if (power < 0)
        power = std::max(1, std::min(10, score / 10));
    QString timing = (QStringList{QStringLiteral("偏早"), QStringLiteral("合适"), QStringLiteral("偏晚")})[randInt(0, 2)];
    QString swing = (QStringList{QStringLiteral("偏小"), QStringLiteral("合适"), QStringLiteral("偏大")})[randInt(0, 2)];

    m_metricSpeed->setText(QString::number(speed));
    m_metricPower->setText(QString::number(power));
    const QString typeLabel = hitType.trimmed().isEmpty() ? skillLabel : hitType.trimmed();
    m_metricTiming->setText(durationMs >= 0 ? QStringLiteral("%1 ms").arg(durationMs)
                                            : timing);
    m_metricSwing->setText(typeLabel);

    m_aiComment->setText(QStringLiteral("「%1」本次击球综合得分 %2 分，可结合右侧回放查看挥拍节奏与击球点。").arg(typeLabel).arg(score));
    ClassHitAdviceContext ctx;
    ctx.hitIdx = idx;
    ctx.hitType = typeLabel;
    ctx.score = score;
    ctx.speedKmh = speed;
    ctx.powerTen = power;
    ctx.durationMs = durationMs;
    m_aiImprove->setText(pickClassHitAiAdvice(ctx));

    for (auto *b : m_speedButtons) {
        if (b->text() == QStringLiteral("1x")) {
            highlightSpeedButton(b, 1.0);
            break;
        }
    }
    if (replayHitNeedsPoseRender(replaySessionId, idx)) {
        requestHitReplayPoseRender(replaySessionId, idx);
    }
    tryStartReplay();
}
