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
#include <QScrollArea>
#include <QFrame>
#include <QResizeEvent>
#include <QEvent>
#include <QFile>
#include <QDir>
#include <QScroller>
#include <QScrollerProperties>
#include <QDebug>
#include <cmath>
#include <algorithm>
#include <numeric>

#include "ui_common.h"
#include "ui_pages.h"
#include "main_window.h"
#include "class_hit_ai_advice.h"
#include "sle_seek_service.h"

namespace {

static constexpr int kMatchMaxPlayers = 4;

static QString normalizeMac(const QString &mac)
{
    return mac.trimmed().toUpper().remove(QLatin1Char(':')).remove(QLatin1Char('-'));
}

static QString imuHitTypeLabel(const QString &imuType, int strokeClassId, float strokeConf)
{
    const QString trimmed = imuType.trimmed();
    if (strokeClassId >= 0 && strokeConf >= 0.15f && !trimmed.isEmpty() && trimmed != QStringLiteral("挥拍"))
        return trimmed;
    if (!trimmed.isEmpty())
        return trimmed;
    return QStringLiteral("挥拍");
}

static int deviceNoFromMac(const QString &mac)
{
    /* 与扫描服务一致：设备号取星闪 MAC 最后一位十六进制数字 */
    QString withColons = mac.trimmed().toUpper();
    withColons.replace(QLatin1Char('-'), QLatin1Char(':'));
    return sleAssignDeviceId(withColons);
}

static QString deviceNameFromMac(const QString &mac)
{
    const int no = deviceNoFromMac(mac);
    return no > 0 ? QStringLiteral("设备%1").arg(no) : QStringLiteral("未知设备");
}

static constexpr qint64 kVisionWindowMs = 1000;

} // namespace

// ═══════════════════════════════════════════════
// 对打/比赛 入口
// ═══════════════════════════════════════════════
MatchPage::MatchPage(MainWindow *mw, QWidget *parent)
    : PageBase(QStringLiteral("对打 / 比赛模式"), "", mw, parent, PageHeaderMode::SingleCentered)
{
    connect(m_backBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(0); });
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();

    auto *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(16);

    auto *leftCol = new QWidget();
    leftCol->setFixedWidth(kSkillDetailLeftColW);
    leftCol->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(12, 8, 4, 12);
    leftLay->setSpacing(0);

    auto *tipsBox = new QFrame();
    tipsBox->setObjectName(QStringLiteral("matchTipsBox"));
    tipsBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    tipsBox->setStyleSheet(
        "QFrame#matchTipsBox { border: 1px solid #234f8c; border-radius: 16px; background: rgba(8,26,51,184); }");
    auto *tbLay = new QVBoxLayout(tipsBox);
    tbLay->setContentsMargins(20, 18, 20, 18);
    tbLay->setSpacing(10);
    auto *tt = new QLabel(QStringLiteral("比赛说明"));
    tt->setStyleSheet(QStringLiteral("font-size:36px; font-weight:800; color:#8cc7ff; background:transparent;"));
    tbLay->addWidget(tt);

    auto addBullet = [&](const QString &text) {
        auto *label = new QLabel(QStringLiteral("· ") + text);
        label->setWordWrap(true);
        label->setStyleSheet(
            QStringLiteral("font-size:28px; font-weight:700; color:#eef4ff; background:transparent;"));
        tbLay->addWidget(label);
    };
    addBullet(QStringLiteral("双人对打或比赛计分，最多接入 4 台九轴拍柄"));
    addBullet(QStringLiteral("击球判定：摄像头稳定识别 / 摄像头挥拍 / 拍柄 IMU 三路 OR，统一去重"));
    addBullet(QStringLiteral("IMU 触发后 ±1 秒内取视觉最高置信度更新动作类型（不加问号）"));
    tbLay->addSpacing(8);
    auto *placeTitle = new QLabel(QStringLiteral("摆放提示"));
    placeTitle->setStyleSheet(QStringLiteral("font-size:30px; font-weight:800; color:#ffb020; background:transparent;"));
    tbLay->addWidget(placeTitle);
    addBullet(QStringLiteral("设备放场地边，正对我方半场正中"));
    addBullet(QStringLiteral("确保摄像头覆盖击球动作与落点区域"));
    tbLay->addStretch(1);
    leftLay->addWidget(tipsBox, 1);
    body->addWidget(leftCol);

    auto *content = new QWidget();
    content->setStyleSheet(QStringLiteral("background:transparent;"));
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(4, 6, 16, 24);
    cl->setSpacing(0);

    const int camPad = 16;
    const int frameW = kCamTrainingW + camPad * 2;
    const int frameH = kCamTrainingH + camPad * 2;
    auto *cameraFrame = new QFrame();
    cameraFrame->setObjectName(QStringLiteral("matchCameraFrame"));
    cameraFrame->setFixedSize(frameW, frameH);
    cameraFrame->setStyleSheet(
        "QFrame#matchCameraFrame { border: 1px solid #2e63ac; border-radius: 18px; background: #000000; }");
    auto *cfLay = new QVBoxLayout(cameraFrame);
    cfLay->setContentsMargins(camPad, camPad, camPad, camPad);
    cfLay->setSpacing(0);

    m_cameraPanel = new QFrame();
    m_cameraPanel->setObjectName(QStringLiteral("matchCam"));
    m_cameraOverlayHost = m_cameraPanel;
    m_cameraPanel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_cameraPanel->setFixedSize(kCamTrainingW, kCamTrainingH);
    m_cameraPanel->setStyleSheet(
        "QFrame#matchCam { border: none; border-radius: 12px; background: #000000; }");
    cfLay->addWidget(m_cameraPanel, 0, Qt::AlignCenter);

    auto *camRow = new QHBoxLayout();
    camRow->setContentsMargins(0, 0, 0, 0);
    camRow->addStretch(1);
    camRow->addWidget(cameraFrame);
    camRow->addStretch(1);
    cl->addLayout(camRow, 1);

    m_deviceStatusLabel = new QLabel(QStringLiteral("尚未连接拍柄，请先扫描绑定"));
    m_deviceStatusLabel->setAlignment(Qt::AlignCenter);
    m_deviceStatusLabel->setWordWrap(true);
    m_deviceStatusLabel->setStyleSheet(
        QStringLiteral("font-size:24px; color:#9eb7de; font-weight:700; background:rgba(7,23,47,160); "
                       "border:1px solid #2e63ac; border-radius:14px; padding:14px 20px;"));
    cl->addWidget(m_deviceStatusLabel);

    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 18, 0, 0);
    btnRow->addStretch(1);

    m_connectBtn = new QPushButton(QStringLiteral("连接拍柄"));
    m_connectBtn->setMinimumWidth(280);
    m_connectBtn->setFixedHeight(132);
    m_connectBtn->setCursor(Qt::PointingHandCursor);
    m_connectBtn->setStyleSheet(QString(R"(
        QPushButton {
            border: 2px solid #2e63ac; border-radius: 22px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #153563, stop:1 #0f2649);
            color: #eaf3ff; font-size: 34px; font-weight: 800; padding: 16px 28px;
        }
        QPushButton:hover { border-color: #56baff; }
    )"));
    connect(m_connectBtn, &QPushButton::clicked, this, [this, mw]() {
        if (mw->m_matchSetup)
            mw->m_matchSetup->resetScan();
        mw->switchPage(16);
    });
    btnRow->addWidget(m_connectBtn);

    btnRow->addSpacing(24);

    m_startBtn = new QPushButton(QStringLiteral("开始比赛"));
    m_startBtn->setMinimumWidth(kSkillDetailStartBtnW);
    m_startBtn->setMaximumWidth(kSkillDetailStartBtnW);
    m_startBtn->setFixedHeight(132);
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setEnabled(false);
    m_startBtn->setStyleSheet(QString(R"(
        QPushButton {
            border: 2px solid %1; border-radius: 22px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #45e08a, stop:1 %2);
            color: #062a14; font-size: 40px; font-weight: 800; padding: 16px 36px;
        }
        QPushButton:hover { border-color: #7ef0b0; }
        QPushButton:disabled { border-color: #3a5a4a; color: #4a6a5a; background: #1a2a24; }
    )").arg(COLOR_OK, COLOR_OK));
    connect(m_startBtn, &QPushButton::clicked, this, [this, mw]() {
        emit startMatch();
        mw->switchPage(8);
    });
    btnRow->addWidget(m_startBtn);
    btnRow->addStretch(1);
    cl->addLayout(btnRow);

    body->addWidget(content, 1);
    m_rootLayout->addLayout(body, 1);
}

void MatchPage::setConnectedPlayers(const QList<MatchPlayerBinding> &players)
{
    if (!m_deviceStatusLabel || !m_startBtn)
        return;
    if (players.isEmpty()) {
        m_deviceStatusLabel->setText(QStringLiteral("尚未连接拍柄，请先扫描绑定（最多 4 台）"));
        m_startBtn->setEnabled(false);
        if (m_connectBtn)
            m_connectBtn->setText(QStringLiteral("连接拍柄"));
        return;
    }
    QStringList names;
    for (const auto &p : players)
        names.append(QStringLiteral("%1").arg(p.deviceName));
    m_deviceStatusLabel->setText(
        QStringLiteral("已连接 %1 台拍柄：%2").arg(players.size()).arg(names.join(QStringLiteral("、"))));
    m_startBtn->setEnabled(true);
    if (m_connectBtn)
        m_connectBtn->setText(QStringLiteral("重新连接"));
}

// ═══════════════════════════════════════════════
// 对打模式：扫描拍柄
// ═══════════════════════════════════════════════
MatchSetupPage::MatchSetupPage(MainWindow *mw, QWidget *parent)
    : PageBase(QStringLiteral("对打模式 · 连接拍柄"), "", mw, parent)
{
    connect(m_backBtn, &QPushButton::clicked, this, [this, mw]() {
        showScanStep();
        mw->switchPage(1);
    });
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();

    m_stepStack = new QStackedWidget();
    m_stepStack->setStyleSheet(QStringLiteral("background: transparent;"));

    m_scanStep = new QWidget();
    m_scanStep->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *scanBody = new QVBoxLayout(m_scanStep);
    scanBody->setContentsMargins(24, 16, 24, 16);
    scanBody->setSpacing(16);

    auto *scanActions = new QHBoxLayout();
    scanActions->addStretch();
    m_scanBtn = new QPushButton(QStringLiteral("扫描设备"));
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    m_scanBtn->setFixedSize(400, 96);
    m_scanBtn->setStyleSheet(R"(
        QPushButton {
            border: 1px solid #2f6ab4; border-radius: 20px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #1d5aa1, stop:1 #123a6f);
            color: #eaf3ff; font-size: 28px; font-weight: 900;
        }
        QPushButton:hover { border-color: #56baff; }
    )");
    connect(m_scanBtn, &QPushButton::clicked, this, &MatchSetupPage::beginScan);
    scanActions->addWidget(m_scanBtn);

    m_scanNextBtn = makePrimaryBtn(QStringLiteral("下一步"));
    m_scanNextBtn->setFixedSize(240, 96);
    m_scanNextBtn->setStyleSheet(m_scanNextBtn->styleSheet() + QStringLiteral("font-size: 22px;"));
    m_scanNextBtn->setEnabled(false);
    connect(m_scanNextBtn, &QPushButton::clicked, this, &MatchSetupPage::goConfirmStep);
    scanActions->addWidget(m_scanNextBtn);
    scanActions->addStretch();
    scanBody->addLayout(scanActions);

    m_scanMsg = new QLabel(QStringLiteral("请扫描对打用拍柄，最多选择 4 台（约 6–8 秒）。"));
    m_scanMsg->setStyleSheet(QStringLiteral("font-size:20px; color:#9eb7de; background:transparent;"));
    m_scanMsg->setAlignment(Qt::AlignCenter);
    m_scanMsg->setWordWrap(true);
    scanBody->addWidget(m_scanMsg);

    auto *resultsFrame = new QFrame();
    resultsFrame->setObjectName(QStringLiteral("card"));
    resultsFrame->setStyleSheet(
        QStringLiteral("QFrame#card { border: 1px solid #234f8c; border-radius: 18px; background: #07172fb0; }"));
    auto *rfLay = new QVBoxLayout(resultsFrame);
    rfLay->setContentsMargins(12, 12, 12, 12);
    auto *rfHead = new QHBoxLayout();
    auto *rfTitle = new QLabel(QStringLiteral("扫描到的星闪设备"));
    rfTitle->setStyleSheet(QStringLiteral("color:#8cc7ff; font-weight:800; background:transparent; font-size:24px;"));
    rfHead->addWidget(rfTitle);
    rfHead->addStretch();
    m_scanCount = new QLabel(QStringLiteral("0 台"));
    m_scanCount->setStyleSheet(QStringLiteral("color:#9eb7de; font-size:22px; background:transparent;"));
    rfHead->addWidget(m_scanCount);
    rfLay->addLayout(rfHead);

    m_deviceScroll = new QScrollArea();
    m_deviceScroll->setWidgetResizable(true);
    m_deviceScroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; }"));
    m_deviceScroll->viewport()->setStyleSheet(QStringLiteral("background: transparent;"));
    m_deviceScroll->viewport()->installEventFilter(this);
    m_deviceGrid = new QWidget();
    m_deviceGrid->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *devGridLay = new QGridLayout();
    devGridLay->setContentsMargins(4, 4, 4, 4);
    devGridLay->setHorizontalSpacing(18);
    devGridLay->setVerticalSpacing(18);
    m_deviceGrid->setLayout(devGridLay);
    m_deviceScroll->setWidget(m_deviceGrid);
    rfLay->addWidget(m_deviceScroll, 1);
    scanBody->addWidget(resultsFrame, 1);
    m_stepStack->addWidget(m_scanStep);

    m_confirmStep = new QWidget();
    m_confirmStep->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *confBody = new QVBoxLayout(m_confirmStep);
    confBody->setContentsMargins(24, 16, 24, 16);
    confBody->setSpacing(14);

    m_confirmHint = new QLabel(QStringLiteral("请选择对打用拍柄（最多 4 台，按选择顺序分配左右站位）。"));
    m_confirmHint->setStyleSheet(QStringLiteral("font-size:20px; color:#cfe2ff; background:transparent;"));
    m_confirmHint->setAlignment(Qt::AlignCenter);
    m_confirmHint->setWordWrap(true);
    confBody->addWidget(m_confirmHint);

    m_confirmScroll = new QScrollArea();
    m_confirmScroll->setWidgetResizable(true);
    m_confirmScroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; }"));
    m_confirmList = new QWidget();
    m_confirmList->setLayout(new QVBoxLayout());
    qobject_cast<QVBoxLayout *>(m_confirmList->layout())->setSpacing(12);
    m_confirmScroll->setWidget(m_confirmList);
    confBody->addWidget(m_confirmScroll, 1);

    auto *confActions = new QHBoxLayout();
    confActions->addStretch();
    auto *backScanBtn = makeGhostBtn(QStringLiteral("重新扫描"));
    backScanBtn->setFixedHeight(88);
    connect(backScanBtn, &QPushButton::clicked, this, [this]() { showScanStep(); });
    confActions->addWidget(backScanBtn);

    m_confirmBtn = makePrimaryBtn(QStringLiteral("确认 · 进入对打"));
    m_confirmBtn->setFixedSize(360, 96);
    m_confirmBtn->setStyleSheet(m_confirmBtn->styleSheet() + QStringLiteral("font-size: 20px;"));
    m_confirmBtn->setEnabled(false);
    connect(m_confirmBtn, &QPushButton::clicked, this, &MatchSetupPage::confirmAndEnterMatch);
    confActions->addWidget(m_confirmBtn);
    confActions->addStretch();
    confBody->addLayout(confActions);
    m_stepStack->addWidget(m_confirmStep);

    m_rootLayout->addWidget(m_stepStack, 1);
    showScanStep();
}

void MatchSetupPage::resetScan()
{
    m_seekDevices.clear();
    m_selectedIndices.clear();
    m_deviceGridCols = 0;
    if (m_scanCount)
        m_scanCount->setText(QStringLiteral("0 台"));
    if (m_scanMsg)
        m_scanMsg->setText(QStringLiteral("请扫描对打用拍柄，最多选择 4 台（约 6–8 秒）。"));
    if (m_deviceGrid && m_deviceGrid->layout()) {
        QLayoutItem *child;
        while ((child = m_deviceGrid->layout()->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
    }
    updateScanNextButton();
    showScanStep();
}

void MatchSetupPage::showScanStep()
{
    if (m_stepStack)
        m_stepStack->setCurrentWidget(m_scanStep);
    m_titleLabel->setText(QStringLiteral("对打模式 · 扫描拍柄"));
}

void MatchSetupPage::showConfirmStep()
{
    if (m_seekDevices.isEmpty()) {
        if (m_scanMsg)
            m_scanMsg->setText(QStringLiteral("请先完成设备扫描"));
        showScanStep();
        return;
    }
    rebuildConfirmList();
    if (m_stepStack)
        m_stepStack->setCurrentWidget(m_confirmStep);
    m_titleLabel->setText(QStringLiteral("对打模式 · 选择拍柄"));
}

int MatchSetupPage::computeDeviceGridCols() const
{
    if (!m_deviceScroll)
        return 2;
    int vw = m_deviceScroll->viewport()->width();
    if (vw < 80)
        vw = 900;
    return qBound(1, qMax(1, (vw + 16) / (440 + 16)), 3);
}

int MatchSetupPage::computeDeviceCellWidth() const
{
    if (!m_deviceScroll)
        return 440;
    int vw = m_deviceScroll->viewport()->width();
    if (vw < 80)
        vw = 900;
    const int cols = computeDeviceGridCols();
    return qMax(400, (vw - (cols + 1) * 16) / cols);
}

void MatchSetupPage::rebuildDeviceGrid()
{
    auto *grid = qobject_cast<QGridLayout *>(m_deviceGrid->layout());
    if (!grid)
        return;
    QLayoutItem *child;
    while ((child = grid->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    const int n = m_seekDevices.size();
    if (n == 0) {
        m_deviceGridCols = 0;
        return;
    }
    const int cols = computeDeviceGridCols();
    const int cellW = computeDeviceCellWidth();
    m_deviceGridCols = cols;

    for (int i = 0; i < n; ++i) {
        const SleSeekDevice &d = m_seekDevices.at(i);
        const QString devName = deviceNameFromMac(d.mac);
        auto *card = new QFrame();
        card->setObjectName(QStringLiteral("dcard"));
        card->setFixedWidth(cellW);
        card->setStyleSheet(
            "QFrame#dcard { border: 2px solid #3a7fd4; border-radius: 18px; "
            "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445); }");
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(20, 16, 20, 16);
        auto *title = new QLabel(QStringLiteral("%1 · %2").arg(devName).arg(d.mac));
        title->setStyleSheet(QStringLiteral("font-size:28px; font-weight:900; color:#ffffff; background:transparent;"));
        title->setWordWrap(true);
        cl->addWidget(title);
        auto *sub = new QLabel(QStringLiteral("信号 %1 dBm").arg(d.rssi));
        sub->setStyleSheet(QStringLiteral("font-size:22px; color:#9eb7de; background:transparent;"));
        cl->addWidget(sub);
        grid->addWidget(card, i / cols, i % cols, Qt::AlignLeft | Qt::AlignTop);
    }
}

void MatchSetupPage::rebuildConfirmList()
{
    auto *lay = qobject_cast<QVBoxLayout *>(m_confirmList->layout());
    if (!lay)
        return;
    QLayoutItem *child;
    while ((child = lay->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    if (m_selectedIndices.isEmpty() && !m_seekDevices.isEmpty())
        m_selectedIndices.insert(0);

    for (int i = 0; i < m_seekDevices.size(); ++i) {
        const SleSeekDevice &d = m_seekDevices.at(i);
        const QString devName = deviceNameFromMac(d.mac);
        auto *row = new QPushButton();
        row->setCheckable(true);
        row->setChecked(m_selectedIndices.contains(i));
        row->setCursor(Qt::PointingHandCursor);
        row->setMinimumHeight(96);
        row->setText(QStringLiteral("%1 · %2 · 信号 %3 dBm")
                         .arg(devName)
                         .arg(d.mac)
                         .arg(d.rssi));
        row->setStyleSheet(R"(
            QPushButton {
                border: 2px solid #2e63ac; border-radius: 14px;
                background: #0e2445; color: #eaf3ff; font-size: 18px; font-weight: 700;
                text-align: left; padding: 14px 18px;
            }
            QPushButton:checked { border-color: #56baff; background: #153a6f; }
            QPushButton:hover { border-color: #56baff; }
        )");
        const int idx = i;
        connect(row, &QPushButton::clicked, this, [this, idx, row]() {
            if (row->isChecked()) {
                if (m_selectedIndices.size() >= kMatchMaxPlayers) {
                    row->setChecked(false);
                    if (m_confirmHint)
                        m_confirmHint->setText(QStringLiteral("最多选择 %1 台拍柄").arg(kMatchMaxPlayers));
                    return;
                }
                m_selectedIndices.insert(idx);
            } else {
                m_selectedIndices.remove(idx);
            }
            if (m_confirmBtn)
                m_confirmBtn->setEnabled(!m_selectedIndices.isEmpty());
            if (m_confirmHint)
                m_confirmHint->setText(QStringLiteral("已选 %1 / %2 台（按设备编号排序分配左右站位）")
                                         .arg(m_selectedIndices.size())
                                         .arg(kMatchMaxPlayers));
        });
        lay->addWidget(row);
    }
    lay->addStretch();
    if (m_confirmBtn)
        m_confirmBtn->setEnabled(!m_selectedIndices.isEmpty());
}

bool MatchSetupPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_deviceScroll->viewport() && event->type() == QEvent::Resize) {
        if (!m_seekDevices.isEmpty()) {
            const int want = computeDeviceGridCols();
            if (want != m_deviceGridCols)
                rebuildDeviceGrid();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MatchSetupPage::beginScan()
{
    if (!m_mainWindow || !m_mainWindow->sleSeek())
        return;
    m_seekDevices.clear();
    m_selectedIndices.clear();
    m_deviceGridCols = 0;
    if (m_scanCount)
        m_scanCount->setText(QStringLiteral("0 台"));
    if (m_deviceGrid && m_deviceGrid->layout()) {
        QLayoutItem *child;
        while ((child = m_deviceGrid->layout()->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
    }
    updateScanNextButton();
    m_scanBtn->setEnabled(false);
    if (m_scanMsg)
        m_scanMsg->setText(QStringLiteral("扫描中…"));
    m_mainWindow->sleSeek()->startScan();
}

void MatchSetupPage::updateScanNextButton()
{
    if (m_scanNextBtn)
        m_scanNextBtn->setEnabled(!m_seekDevices.isEmpty());
}

void MatchSetupPage::goConfirmStep()
{
    if (m_seekDevices.isEmpty()) {
        if (m_scanMsg)
            m_scanMsg->setText(QStringLiteral("请先完成设备扫描"));
        return;
    }
    if (m_mainWindow && m_mainWindow->sleSeek() && m_mainWindow->sleSeek()->isScanning())
        m_mainWindow->sleSeek()->stopScan();
    showConfirmStep();
}

void MatchSetupPage::confirmAndEnterMatch()
{
    if (m_selectedIndices.isEmpty())
        return;
    QList<int> ordered = m_selectedIndices.values();
    std::sort(ordered.begin(), ordered.end());
    QList<MatchPlayerBinding> players;
    for (int idx : ordered) {
        if (idx < 0 || idx >= m_seekDevices.size())
            continue;
        const SleSeekDevice &d = m_seekDevices.at(idx);
        const int devNo = deviceNoFromMac(d.mac);
        MatchPlayerBinding b;
        b.deviceId = devNo > 0 ? devNo : (d.deviceId > 0 ? d.deviceId : 0);
        b.deviceCode = devNo > 0 ? QString::number(devNo) : d.mac;
        b.deviceName = deviceNameFromMac(d.mac);
        b.mac = d.mac;
        players.append(b);
    }
    std::sort(players.begin(), players.end(), [](const MatchPlayerBinding &a, const MatchPlayerBinding &b) {
        const int da = a.deviceId > 0 ? a.deviceId : 999;
        const int db = b.deviceId > 0 ? b.deviceId : 999;
        return da < db;
    });
    emit playersConfirmed(players);
}

void MatchSetupPage::onSeekStatus(const QString &msg)
{
    if (!msg.isEmpty() && m_scanMsg)
        m_scanMsg->setText(msg);
}

void MatchSetupPage::applyScanResults(const QList<SleSeekDevice> &devices)
{
    m_seekDevices = devices;
    if (m_scanCount)
        m_scanCount->setText(QStringLiteral("%1 台").arg(devices.size()));
    rebuildDeviceGrid();
    updateScanNextButton();
    if (m_scanBtn)
        m_scanBtn->setEnabled(true);
    if (m_scanMsg && !devices.isEmpty())
        m_scanMsg->setText(QStringLiteral("扫描完成，共 %1 台设备").arg(devices.size()));
}

void MatchSetupPage::afterScanUiReady()
{
    if (m_scanBtn)
        m_scanBtn->setEnabled(true);
}

// ═══════════════════════════════════════════════
// 比赛中
// ═══════════════════════════════════════════════
MatchRunningPage::MatchRunningPage(MainWindow *mw, QWidget *parent)
    : PageBase(QStringLiteral("比赛中"), "", mw, parent, PageHeaderMode::SingleCentered)
    , m_hits(0)
    , m_speedSum(0)
    , m_speedCount(0)
    , m_maxSpeed(0)
    , m_powerSum(0)
    , m_powerCount(0)
    , m_startedAt(0)
    , m_endedAt(0)
{
    connect(m_homeBtn, &QPushButton::clicked, this, [this, mw]() {
        stopRunning();
        mw->goHome();
    });
    m_backBtn->hide();

    m_bodyHost = new QWidget();
    m_bodyHost->setStyleSheet(QStringLiteral("background:transparent;"));
    m_matchGrid = new QGridLayout(m_bodyHost);
    m_matchGrid->setContentsMargins(0, 4, 0, 0);
    m_matchGrid->setHorizontalSpacing(14);
    m_matchGrid->setVerticalSpacing(0);

    m_rootLayout->addWidget(m_bodyHost, 1);

    m_visionPollTimer = new QTimer(this);
    m_visionPollTimer->setInterval(150);
    connect(m_visionPollTimer, &QTimer::timeout, this, &MatchRunningPage::pollPendingVisionHits);
}

void MatchRunningPage::clearMatchBody()
{
    if (!m_matchGrid)
        return;
    QLayoutItem *child;
    while ((child = m_matchGrid->takeAt(0)) != nullptr) {
        if (child->widget())
            child->widget()->deleteLater();
        delete child;
    }
    m_leftCol = nullptr;
    m_rightCol = nullptr;
    m_centerCol = nullptr;
    m_cameraCell = nullptr;
    m_cameraPanel = nullptr;
    m_cameraOverlayHost = nullptr;
    for (auto &ps : m_playerStats) {
        ps.panel = nullptr;
        ps.nameLabel = nullptr;
        ps.aiScoreValue = nullptr;
        ps.hitCountLabel = nullptr;
        ps.speedLabel = nullptr;
        ps.powerLabel = nullptr;
        ps.avgSpeedLabel = nullptr;
        ps.maxSpeedLabel = nullptr;
        ps.actionTypeLabel = nullptr;
        ps.adviceLabel = nullptr;
        ps.statusHint = nullptr;
    }
}

QWidget *MatchRunningPage::buildPlayerPanel(PlayerStats &ps, const QString &sideTitle, MatchPanelStyle style,
                                            bool showAdvice)
{
    static const char *kPanelStyle =
        "QFrame#trainPanel { border: 1px solid #234f8c; border-radius: 16px; "
        "background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 rgba(10,26,51,199), stop:1 rgba(8,22,45,209)); }";

    const bool trainingLike = (style == MatchPanelStyle::TrainingLike);
    const bool quadStack = (style == MatchPanelStyle::CompactQuad);

    auto makeMetricChip = [&](const QString &title, QLabel *&value, const QString &unitHint = QString()) -> QFrame * {
        auto *f = new QFrame();
        f->setObjectName(QStringLiteral("trainMetricChip"));
        f->setMinimumHeight(trainingLike ? 96 : (quadStack ? 84 : 104));
        if (trainingLike) {
        f->setStyleSheet(
            "QFrame#trainMetricChip { border: 1px solid #2e63ac; border-radius: 14px; background: rgba(7,23,47,200); }");
        } else {
            f->setStyleSheet(QStringLiteral("QFrame#trainMetricChip { border: none; background: transparent; }"));
        }
        auto *fl = new QVBoxLayout(f);
        fl->setContentsMargins(trainingLike ? 14 : 6, trainingLike ? 12 : 8, trainingLike ? 14 : 6,
                               trainingLike ? 12 : 8);
        fl->setSpacing(trainingLike ? 4 : 6);
        auto *tl = new QLabel(title);
        tl->setStyleSheet(QStringLiteral("font-size:%1px; color:#8cc7ff; font-weight:700; background:transparent;")
                              .arg(trainingLike ? 14 : (quadStack ? 16 : 18)));
        fl->addWidget(tl);
        value = new QLabel(QStringLiteral("--"));
        value->setStyleSheet(QStringLiteral("font-size:%1px; font-weight:900; color:#eaf3ff; background:transparent;")
                                 .arg(trainingLike ? 28 : (quadStack ? 28 : 34)));
        fl->addWidget(value);
        if (!unitHint.isEmpty()) {
            auto *ul = new QLabel(unitHint);
            ul->setStyleSheet(QStringLiteral("font-size:%1px; color:#9eb7de; background:transparent;")
                                  .arg(trainingLike ? 13 : 15));
            fl->addWidget(ul);
        }
        return f;
    };

    auto *wrap = new QWidget();
    wrap->setMinimumWidth(trainingLike ? kTrainLeftColW : 340);
    wrap->setMaximumWidth(trainingLike ? kTrainLeftColW + 48 : 380);
    wrap->setSizePolicy(QSizePolicy::Fixed, trainingLike ? QSizePolicy::Preferred : QSizePolicy::Expanding);
    if (!trainingLike)
        wrap->setMinimumHeight(quadStack ? (kCamTrainingH / 2 - 36) : (kCamTrainingH - 80));

    auto *vl = new QVBoxLayout(wrap);
    vl->setContentsMargins(0, trainingLike ? 36 : 8, 0, 4);
    vl->setSpacing(trainingLike ? 12 : 10);
    if (trainingLike)
        vl->addStretch(2);

    if (!sideTitle.isEmpty() && !trainingLike) {
        ps.nameLabel = new QLabel(sideTitle);
        ps.nameLabel->setStyleSheet(
            QStringLiteral("font-size:%1px; font-weight:900; color:#ffcf66; background:transparent;")
                .arg(quadStack ? 24 : 28));
        ps.nameLabel->setAlignment(Qt::AlignCenter);
        ps.nameLabel->setMinimumHeight(44);
        vl->addWidget(ps.nameLabel);
    }

    auto *scorePanel = new QFrame();
    scorePanel->setObjectName(QStringLiteral("trainPanel"));
    scorePanel->setMinimumHeight(trainingLike ? 360 : (quadStack ? 300 : 560));
    scorePanel->setSizePolicy(QSizePolicy::Preferred, trainingLike ? QSizePolicy::Preferred : QSizePolicy::Expanding);
    scorePanel->setStyleSheet(kPanelStyle);
    auto *spLay = new QVBoxLayout(scorePanel);
    spLay->setContentsMargins(trainingLike ? 16 : 18, trainingLike ? 16 : 20, trainingLike ? 16 : 18,
                              trainingLike ? 16 : 20);
    spLay->setSpacing(trainingLike ? 12 : 16);

    if (trainingLike) {
    auto *kpiRow = new QGridLayout();
    kpiRow->setContentsMargins(0, 0, 0, 0);
    kpiRow->setHorizontalSpacing(12);
    kpiRow->setColumnStretch(0, 5);
    kpiRow->setColumnStretch(1, 6);

    auto *kpiCard = new QFrame();
    kpiCard->setObjectName(QStringLiteral("trainKpiCard"));
    kpiCard->setMinimumHeight(200);
    kpiCard->setStyleSheet(
        "QFrame#trainKpiCard { border: 1px solid #2e63ac; border-radius: 14px; background: rgba(7,23,47,210); }");
    auto *kpiLay = new QVBoxLayout(kpiCard);
    kpiLay->setContentsMargins(16, 14, 16, 14);
    kpiLay->setSpacing(6);
        auto *scoreTitle = new QLabel(QStringLiteral("AI 打分"));
        scoreTitle->setStyleSheet(
            QStringLiteral("color:#8cc7ff; font-size:%1px; font-weight:700; background:transparent;").arg(26));
        kpiLay->addWidget(scoreTitle);
    auto *scoreRow = new QHBoxLayout();
    scoreRow->setSpacing(8);
        ps.aiScoreValue = new QLabel(QStringLiteral("--"));
        ps.aiScoreValue->setStyleSheet(
        QStringLiteral("font-size:%1px; font-weight:900; line-height:0.95; color:#eaf3ff; background:transparent;").arg(80));
        auto *unit = new QLabel(QStringLiteral("分"));
        unit->setStyleSheet(
            QStringLiteral("color:#b9cff1; font-size:%1px; font-weight:700; background:transparent;").arg(38));
        scoreRow->addWidget(ps.aiScoreValue, 0, Qt::AlignBottom);
        scoreRow->addWidget(unit, 0, Qt::AlignBottom);
    scoreRow->addStretch();
    kpiLay->addLayout(scoreRow);
    kpiLay->addStretch();

    auto *metricsWrap = new QWidget();
    auto *metricsGrid = new QGridLayout(metricsWrap);
    metricsGrid->setContentsMargins(0, 0, 0, 0);
    metricsGrid->setSpacing(10);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("击球次数"), ps.hitCountLabel, QStringLiteral("次")), 0, 0);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("球速"), ps.speedLabel, QStringLiteral("km/h")), 0, 1);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("击球力度"), ps.powerLabel, QStringLiteral("/10")), 1, 0);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("平均球速"), ps.avgSpeedLabel, QStringLiteral("km/h")), 1, 1);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("击球类型"), ps.actionTypeLabel), 2, 0, 1, 2);

    kpiRow->addWidget(kpiCard, 0, 0, Qt::AlignTop);
    kpiRow->addWidget(metricsWrap, 0, 1, Qt::AlignTop);
    spLay->addLayout(kpiRow);
    } else {
        auto *scoreTitle = new QLabel(QStringLiteral("AI 打分"));
        scoreTitle->setStyleSheet(
            QStringLiteral("color:#8cc7ff; font-size:22px; font-weight:700; background:transparent;"));
        spLay->addWidget(scoreTitle);

        auto *scoreRow = new QHBoxLayout();
        scoreRow->setSpacing(10);
        ps.aiScoreValue = new QLabel(QStringLiteral("--"));
        ps.aiScoreValue->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        ps.aiScoreValue->setStyleSheet(
            QStringLiteral("font-size:%1px; font-weight:900; line-height:0.95; color:#eaf3ff; background:transparent;")
                .arg(quadStack ? 56 : 76));
        auto *unit = new QLabel(QStringLiteral("分"));
        unit->setStyleSheet(
            QStringLiteral("color:#b9cff1; font-size:26px; font-weight:700; background:transparent;"));
        scoreRow->addWidget(ps.aiScoreValue, 0, Qt::AlignBottom);
        scoreRow->addWidget(unit, 0, Qt::AlignBottom);
        scoreRow->addStretch();
        spLay->addLayout(scoreRow);

        auto *metricsGrid = new QGridLayout();
        metricsGrid->setContentsMargins(0, 4, 0, 0);
        metricsGrid->setHorizontalSpacing(10);
        metricsGrid->setVerticalSpacing(12);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("击球次数"), ps.hitCountLabel, QStringLiteral("次")), 0, 0);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("球速"), ps.speedLabel, QStringLiteral("km/h")), 0, 1);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("击球力度"), ps.powerLabel, QStringLiteral("/10")), 1, 0);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("平均球速"), ps.avgSpeedLabel, QStringLiteral("km/h")), 1, 1);
        metricsGrid->addWidget(makeMetricChip(QStringLiteral("击球类型"), ps.actionTypeLabel), 2, 0, 1, 2);
        spLay->addLayout(metricsGrid);
    }

    ps.statusHint = new QLabel(QStringLiteral("比赛进行中，请开始击球…"));
    ps.statusHint->setStyleSheet(
        QStringLiteral("color:#cfe2ff; font-size:%1px; line-height:1.45; background:transparent;")
            .arg(trainingLike ? 16 : (quadStack ? 16 : 20)));
    ps.statusHint->setWordWrap(true);
    ps.statusHint->setMinimumHeight(trainingLike ? 0 : (quadStack ? 36 : 52));
    if (!quadStack)
        spLay->addWidget(ps.statusHint);

    if (!trainingLike && showAdvice) {
        auto *advTitle = new QLabel(QStringLiteral("AI 建议"));
        advTitle->setStyleSheet(
            QStringLiteral("color:#8cc7ff; font-size:%1px; font-weight:700; background:transparent;").arg(29));
        spLay->addWidget(advTitle);
        ps.adviceLabel = new QLabel(QStringLiteral("完成击球后将显示智能教练建议。"));
        ps.adviceLabel->setWordWrap(true);
        ps.adviceLabel->setMinimumHeight(72);
        ps.adviceLabel->setStyleSheet(
            QStringLiteral("color:#d9e9ff; font-size:%1px; line-height:1.55; background:transparent;").arg(26));
        spLay->addWidget(ps.adviceLabel, 1);
    }

    vl->addWidget(scorePanel, trainingLike ? 5 : 1);

    if (trainingLike) {
        auto *corrPanel = new QFrame();
        corrPanel->setObjectName(QStringLiteral("trainPanel"));
        corrPanel->setMinimumHeight(200);
        corrPanel->setStyleSheet(kPanelStyle);
        auto *cpLay = new QVBoxLayout(corrPanel);
        cpLay->setContentsMargins(16, 14, 16, 14);
        cpLay->setSpacing(8);
        auto *advTitle = new QLabel(QStringLiteral("AI 建议"));
        advTitle->setStyleSheet(
            QStringLiteral("color:#8cc7ff; font-size:%1px; font-weight:700; background:transparent;").arg(25));
        cpLay->addWidget(advTitle);
        ps.adviceLabel = new QLabel(QStringLiteral("完成击球后将显示智能教练建议。"));
        ps.adviceLabel->setWordWrap(true);
        ps.adviceLabel->setStyleSheet(
            QStringLiteral("color:#d9e9ff; font-size:%1px; line-height:1.55; background:transparent;").arg(24));
        cpLay->addWidget(ps.adviceLabel, 1);
        vl->addWidget(corrPanel, 4);
    }

    ps.panel = wrap;
    return wrap;
}

void MatchRunningPage::rebuildMatchUi()
{
    clearMatchBody();

    m_cameraPanel = new QFrame();
    m_cameraPanel->setObjectName(QStringLiteral("trainCam"));
    m_cameraOverlayHost = m_cameraPanel;
    m_cameraPanel->setAttribute(Qt::WA_TranslucentBackground, false);
    m_cameraPanel->setFixedSize(kCamTrainingW, kCamTrainingH);
    m_cameraPanel->setStyleSheet(
        "QFrame#trainCam { border: 1px solid #234f8c; border-radius: 16px; background: #000000; }");

    m_cameraCell = new QWidget();
    auto *camLay = new QHBoxLayout(m_cameraCell);
    camLay->setContentsMargins(0, 0, 0, 0);
    camLay->addStretch(1);
    camLay->addWidget(m_cameraPanel, 0, Qt::AlignCenter);
    camLay->addStretch(1);

    const int n = m_playerStats.size();
    if (n <= 0)
        return;

    auto *endBtn = new QPushButton(QStringLiteral("结束比赛"));
    endBtn->setCursor(Qt::PointingHandCursor);
    endBtn->setFixedHeight(76);
    endBtn->setStyleSheet(QString(R"(
        QPushButton {
            border: 1px solid #7a2b2b; border-radius: 16px;
            background: qlineargradient(x1:0,y1:0,x2:0,y2:1, stop:0 #5a1f1f, stop:1 #3a1414);
            color: #fff3f3; font-size: 19px; font-weight: 700;
        }
        QPushButton:hover { border-color: #ff6b6b; }
    )"));
    connect(endBtn, &QPushButton::clicked, this, [this]() {
        stopRunning();
        emit matchEnded();
    });

    if (n == 1) {
        m_leftCol = buildPlayerPanel(m_playerStats[0], QString(), MatchPanelStyle::TrainingLike);
        auto *btnWrap = new QWidget();
        auto *bl = new QVBoxLayout(btnWrap);
        bl->setContentsMargins(0, 4, 0, 0);
        bl->addWidget(endBtn);
        qobject_cast<QVBoxLayout *>(m_leftCol->layout())->addWidget(btnWrap);
        m_matchGrid->addWidget(m_leftCol, 0, 0, Qt::AlignTop);
        m_matchGrid->addWidget(m_cameraCell, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
        m_matchGrid->setColumnStretch(0, 0);
        m_matchGrid->setColumnStretch(1, 1);
    } else if (n == 2) {
        m_leftCol = buildPlayerPanel(m_playerStats[0], m_playerStats[0].binding.deviceName,
                                     MatchPanelStyle::CompactDual);
        m_rightCol = buildPlayerPanel(m_playerStats[1], m_playerStats[1].binding.deviceName,
                                      MatchPanelStyle::CompactDual);
        auto *btnWrap = new QWidget();
        auto *bl = new QVBoxLayout(btnWrap);
        bl->setContentsMargins(0, 8, 0, 0);
        bl->addWidget(endBtn);
        qobject_cast<QVBoxLayout *>(m_rightCol->layout())->addWidget(btnWrap);
        m_matchGrid->addWidget(m_leftCol, 0, 0, Qt::AlignTop);
        m_matchGrid->addWidget(m_cameraCell, 0, 1, Qt::AlignCenter);
        m_matchGrid->addWidget(m_rightCol, 0, 2, Qt::AlignTop);
        m_matchGrid->setRowStretch(0, 1);
        m_matchGrid->setColumnStretch(0, 0);
        m_matchGrid->setColumnStretch(1, 1);
        m_matchGrid->setColumnStretch(2, 0);
    } else if (n == 4) {
        m_leftCol = new QWidget();
        auto *leftLay = new QVBoxLayout(m_leftCol);
        leftLay->setContentsMargins(0, 8, 0, 0);
        leftLay->setSpacing(10);
        leftLay->addWidget(buildPlayerPanel(m_playerStats[0], m_playerStats[0].binding.deviceName,
                                            MatchPanelStyle::CompactQuad, false));
        leftLay->addWidget(buildPlayerPanel(m_playerStats[1], m_playerStats[1].binding.deviceName,
                                            MatchPanelStyle::CompactQuad, false));
        leftLay->addStretch(1);

        m_rightCol = new QWidget();
        auto *rightLay = new QVBoxLayout(m_rightCol);
        rightLay->setContentsMargins(0, 8, 0, 0);
        rightLay->setSpacing(10);
        rightLay->addWidget(buildPlayerPanel(m_playerStats[2], m_playerStats[2].binding.deviceName,
                                            MatchPanelStyle::CompactQuad, false));
        rightLay->addWidget(buildPlayerPanel(m_playerStats[3], m_playerStats[3].binding.deviceName,
                                            MatchPanelStyle::CompactQuad, false));
        auto *btnWrap = new QWidget();
        auto *bl = new QVBoxLayout(btnWrap);
        bl->setContentsMargins(0, 8, 0, 0);
        bl->addWidget(endBtn);
        rightLay->addWidget(btnWrap);
        rightLay->addStretch(1);

        m_matchGrid->addWidget(m_leftCol, 0, 0, Qt::AlignTop);
        m_matchGrid->addWidget(m_cameraCell, 0, 1, Qt::AlignCenter);
        m_matchGrid->addWidget(m_rightCol, 0, 2, Qt::AlignTop);
        m_matchGrid->setRowStretch(0, 1);
        m_matchGrid->setColumnStretch(0, 0);
        m_matchGrid->setColumnStretch(1, 1);
        m_matchGrid->setColumnStretch(2, 0);
    } else {
        m_leftCol = new QWidget();
        auto *scroll = new QScrollArea();
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet(QStringLiteral("QScrollArea { border: none; background: transparent; }"));
        auto *inner = new QWidget();
        auto *innerLay = new QVBoxLayout(inner);
        innerLay->setSpacing(12);
        innerLay->setContentsMargins(0, 0, 0, 0);
        for (int i = 0; i < n; ++i) {
            innerLay->addWidget(buildPlayerPanel(m_playerStats[i], m_playerStats[i].binding.deviceName,
                                                 MatchPanelStyle::CompactDual));
        }
        innerLay->addStretch();
        scroll->setWidget(inner);
        auto *ll = new QVBoxLayout(m_leftCol);
        ll->setContentsMargins(0, 8, 0, 0);
        ll->addWidget(scroll, 1);
        ll->addWidget(endBtn);
        m_matchGrid->addWidget(m_leftCol, 0, 0, Qt::AlignTop);
        m_matchGrid->addWidget(m_cameraCell, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
        m_matchGrid->setColumnStretch(0, 0);
        m_matchGrid->setColumnStretch(1, 1);
    }
}

void MatchRunningPage::initPlayers(const QList<MatchPlayerBinding> &players)
{
    m_players = players;
    m_playerStats.clear();
    for (const auto &p : players) {
        PlayerStats ps;
        ps.binding = p;
        m_playerStats.append(ps);
    }
    rebuildMatchUi();
}

int MatchRunningPage::playerIndexForMac(const QString &mac) const
{
    const QString key = normalizeMac(mac);
    if (key.isEmpty())
        return -1;
    for (int i = 0; i < m_playerStats.size(); ++i) {
        if (normalizeMac(m_playerStats[i].binding.mac) == key)
            return i;
    }
    return -1;
}

int MatchRunningPage::resolvePlayerForCameraHit() const
{
    if (m_playerStats.isEmpty())
        return -1;
    if (m_playerStats.size() == 1)
        return 0;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int found = -1;
    for (int i = 0; i < m_playerStats.size(); ++i) {
        const qint64 sigMs = m_playerStats[i].lastImuSignalMs;
        if (sigMs <= 0)
            continue;
        const qint64 dt = now - sigMs;
        if (dt < 0 || dt >= practiceHitCooldownMs())
            continue;
        if (found >= 0)
            return -1;
        found = i;
    }
    return found;
}

bool MatchRunningPage::tryAcceptPlayerHit(PlayerStats &ps)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (ps.lastHitMs > 0 && (now - ps.lastHitMs) < practiceHitCooldownMs())
        return false;
    ps.lastHitMs = now;
    return true;
}

void MatchRunningPage::refreshPlayerPanel(int playerIdx)
{
    if (playerIdx < 0 || playerIdx >= m_playerStats.size())
        return;
    const PlayerStats &ps = m_playerStats[playerIdx];
    const int avg = ps.speedCount > 0 ? ps.speedSum / ps.speedCount : 0;
    const int avgScore = ps.scores.isEmpty()
        ? 0
        : std::accumulate(ps.scores.begin(), ps.scores.end(), 0) / ps.scores.size();

    if (ps.aiScoreValue)
        ps.aiScoreValue->setText(ps.scores.isEmpty() ? QStringLiteral("--") : QString::number(avgScore));
    if (ps.hitCountLabel)
        ps.hitCountLabel->setText(QString::number(ps.hits));
    if (ps.avgSpeedLabel)
        ps.avgSpeedLabel->setText(avg > 0 ? QString::number(avg) : QStringLiteral("--"));
    if (ps.statusHint) {
    const int elapsedSec = m_startedAt > 0
        ? static_cast<int>((QDateTime::currentMSecsSinceEpoch() - m_startedAt) / 1000)
        : 0;
        if (ps.hits <= 0) {
            ps.statusHint->setText(QStringLiteral("比赛进行中 · 已用时 %1 秒 · 等待首次击球…").arg(elapsedSec));
        } else {
            ps.statusHint->setText(QStringLiteral("比赛进行中 · 已用时 %1 秒 · 已记录 %2 次击球 · 平均球速 %3 km/h")
                              .arg(elapsedSec)
                                       .arg(ps.hits)
                              .arg(avg > 0 ? avg : 0));
}
    }
}

void MatchRunningPage::onPlayerImuHit(int playerIdx, double speedKmh, int powerTen, int durationMs,
                                      const QString &imuType, int strokeClassId, float strokeConf)
{
    if (playerIdx < 0 || playerIdx >= m_playerStats.size())
        return;
    PlayerStats &ps = m_playerStats[playerIdx];

    const int speed = displaySpeedKmh(speedKmh);
    const int power = qBound(1, powerTen, 10);
    const QString typeLabel = imuHitTypeLabel(imuType, strokeClassId, strokeConf);
    const int score = classHitScoreFromImu(strokeClassId, typeLabel, strokeConf, power);

    ps.hits++;
    ps.speedSum += speed;
    ps.speedCount++;
    ps.maxSpeed = std::max(ps.maxSpeed, speed);
    ps.powerSum += power;
    ps.powerCount++;
    ps.scores.append(score);

    m_hits++;
    m_speedSum += speed;
    m_speedCount++;
    m_maxSpeed = std::max(m_maxSpeed, speed);
    m_powerSum += power;
    m_powerCount++;

    if (ps.speedLabel)
        ps.speedLabel->setText(QString::number(speed));
    if (ps.powerLabel)
        ps.powerLabel->setText(QString::number(power));
    if (ps.actionTypeLabel)
        ps.actionTypeLabel->setText(QStringLiteral("识别中…"));

    ClassHitAdviceContext ctx;
    ctx.studentName = ps.binding.deviceName;
    ctx.hitIdx = ps.hits;
    ctx.hitType = typeLabel;
    ctx.score = score;
    ctx.speedKmh = speed;
    ctx.powerTen = power;
    ctx.durationMs = durationMs;
    if (ps.adviceLabel)
        ps.adviceLabel->setText(pickClassHitAiAdvice(ctx));

    MatchHitRecord provisional;
    provisional.playerName = ps.binding.deviceName;
    provisional.actionType = typeLabel;
    provisional.score = score;
    provisional.aiSuggestion = pickClassHitAiAdvice(ctx);
    provisional.speedKmh = speed;
    provisional.powerTen = power;
    provisional.hitIdx = m_hits;
    provisional.durationMs = durationMs;
    m_hitRecords.append(provisional);

    refreshPlayerPanel(playerIdx);

    const qint64 triggerMs = QDateTime::currentMSecsSinceEpoch();
    const int replayHitIdx = m_hits;
    registerHitReplay(replayHitIdx);
    scheduleVisionResolve(playerIdx, ps.hits, replayHitIdx, triggerMs, speed, power, durationMs, typeLabel,
                          strokeClassId, strokeConf, score);
}

void MatchRunningPage::onPlayerCameraHit(int playerIdx, int clsId, const QString &nameCn, float camScore)
{
    if (playerIdx < 0 || playerIdx >= m_playerStats.size())
        return;
    PlayerStats &ps = m_playerStats[playerIdx];

    const QString hitType = nameCn.trimmed().isEmpty() ? QStringLiteral("挥拍") : nameCn.trimmed();
    constexpr int kCamPowerNeutral = 5;
    const int score = classHitScoreFromImu(clsId, hitType, camScore, kCamPowerNeutral);

    ps.hits++;
    ps.scores.append(score);
    m_hits++;

    if (ps.speedLabel)
        ps.speedLabel->setText(QStringLiteral("--"));
    if (ps.powerLabel)
        ps.powerLabel->setText(QStringLiteral("--"));
    if (ps.actionTypeLabel)
        ps.actionTypeLabel->setText(hitType);

    ClassHitAdviceContext ctx;
    ctx.studentName = ps.binding.deviceName;
    ctx.hitIdx = ps.hits;
    ctx.hitType = hitType;
    ctx.score = score;
    ctx.speedKmh = -1;
    ctx.powerTen = -1;
    ctx.durationMs = -1;
    if (ps.adviceLabel)
        ps.adviceLabel->setText(pickClassHitAiAdvice(ctx));

    MatchHitRecord rec;
    rec.playerName = ps.binding.deviceName;
    rec.actionType = hitType;
    rec.score = score;
    rec.aiSuggestion = pickClassHitAiAdvice(ctx);
    rec.speedKmh = 0;
    rec.powerTen = kCamPowerNeutral;
    rec.hitIdx = m_hits;
    rec.durationMs = -1;
    m_hitRecords.append(rec);

    registerHitReplay(m_hits);
    refreshPlayerPanel(playerIdx);
}

void MatchRunningPage::onYoloActionUpdated(int clsId, const QString &nameCn, const QString &, float score, bool stable)
{
    Q_UNUSED(nameCn);
    if (!m_cameraHitSubscribed)
        return;

    const float minStable = practiceCameraStableConf();
    const bool becameStableHigh = stable && clsId >= 0 && score >= minStable &&
        (!m_prevYoloStable || m_prevYoloCls != clsId || m_prevYoloScore < minStable);
    m_prevYoloStable = stable;
    m_prevYoloCls = clsId;
    m_prevYoloScore = score;

    if (!becameStableHigh)
        return;

    const int playerIdx = resolvePlayerForCameraHit();
    if (playerIdx < 0)
        return;
    if (!tryAcceptPlayerHit(m_playerStats[playerIdx]))
        return;
    onPlayerCameraHit(playerIdx, clsId, nameCn, score);
}

void MatchRunningPage::onCameraSwingDetected(int swingSeq, int clsId, const QString &nameCn, float score)
{
    Q_UNUSED(swingSeq);
    if (!m_cameraHitSubscribed)
        return;
    if (score < practiceCameraMinSwingScore())
        return;

    const int playerIdx = resolvePlayerForCameraHit();
    if (playerIdx < 0)
        return;
    if (!tryAcceptPlayerHit(m_playerStats[playerIdx]))
        return;
    onPlayerCameraHit(playerIdx, clsId, nameCn, score);
}

void MatchRunningPage::scheduleVisionResolve(int playerIdx, int hitIdx, int replayHitIdx, qint64 triggerMs,
                                             int speedKmh, int powerTen, int durationMs, const QString &imuType,
                                             int strokeClassId, float strokeConf, int provisionalScore)
{
    PendingVisionHit p;
    p.playerIdx = playerIdx;
    p.hitIdx = hitIdx;
    p.replayHitIdx = replayHitIdx;
    p.triggerMs = triggerMs;
    p.resolveAfterMs = triggerMs + kVisionWindowMs;
    p.speedKmh = speedKmh;
    p.powerTen = powerTen;
    p.durationMs = durationMs;
    p.imuType = imuType;
    p.imuClsId = strokeClassId;
    p.imuConf = strokeConf;
    p.provisionalScore = provisionalScore;
    m_pendingHits.append(p);
    if (m_visionPollTimer && !m_visionPollTimer->isActive())
        m_visionPollTimer->start();
}

void MatchRunningPage::finalizeHitVision(PendingVisionHit &pending)
{
    if (pending.resolved || pending.playerIdx < 0 || pending.playerIdx >= m_playerStats.size())
        return;
    PlayerStats &ps = m_playerStats[pending.playerIdx];

    int visionCls = -1;
    float visionScore = 0.0f;
    QString visionType;
    bool hasVision = false;
    if (m_mainWindow && m_mainWindow->yoloAction()) {
        hasVision = m_mainWindow->yoloAction()->bestSwingInWindow(pending.triggerMs, kVisionWindowMs, visionCls,
                                                                  visionScore, visionType);
    }

    QString finalType = imuHitTypeLabel(pending.imuType, pending.imuClsId, pending.imuConf);
    int finalScore = classHitScoreFromImu(pending.imuClsId, finalType, pending.imuConf, pending.powerTen);
    if (hasVision && !visionType.isEmpty() && visionType != QStringLiteral("—")) {
        finalType = visionType;
        finalScore = classHitScoreFromImu(visionCls, finalType, visionScore, pending.powerTen);
    }
    if (pending.hitIdx > 0 && pending.hitIdx <= ps.scores.size())
        ps.scores[pending.hitIdx - 1] = finalScore;

    if (ps.actionTypeLabel)
        ps.actionTypeLabel->setText(finalType);

    ClassHitAdviceContext ctx;
    ctx.studentName = ps.binding.deviceName;
    ctx.hitIdx = pending.hitIdx;
    ctx.hitType = finalType;
    ctx.score = finalScore;
    ctx.speedKmh = pending.speedKmh;
    ctx.powerTen = pending.powerTen;
    ctx.durationMs = pending.durationMs;
    if (ps.adviceLabel)
        ps.adviceLabel->setText(pickClassHitAiAdvice(ctx));

    /* 更新已写入的临时记录，避免同一击球在报告里出现两次 */
    const int replayIdx = pending.replayHitIdx > 0 ? pending.replayHitIdx : pending.hitIdx;
    bool updated = false;
    for (auto &rec : m_hitRecords) {
        if (rec.hitIdx == replayIdx) {
            rec.playerName = ps.binding.deviceName;
            rec.actionType = finalType;
            rec.score = finalScore;
            rec.aiSuggestion = pickClassHitAiAdvice(ctx);
            rec.speedKmh = pending.speedKmh;
            rec.powerTen = pending.powerTen;
            rec.durationMs = pending.durationMs;
            updated = true;
            break;
        }
    }
    if (!updated) {
        MatchHitRecord rec;
        rec.playerName = ps.binding.deviceName;
        rec.actionType = finalType;
        rec.score = finalScore;
        rec.aiSuggestion = pickClassHitAiAdvice(ctx);
        rec.speedKmh = pending.speedKmh;
        rec.powerTen = pending.powerTen;
        rec.hitIdx = replayIdx;
        rec.durationMs = pending.durationMs;
        m_hitRecords.append(rec);
    }

    refreshPlayerPanel(pending.playerIdx);
    pending.resolved = true;
}

void MatchRunningPage::pollPendingVisionHits()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto &p : m_pendingHits) {
        if (p.resolved)
            continue;
        if (now >= p.resolveAfterMs)
            finalizeHitVision(p);
    }
    m_pendingHits.erase(
        std::remove_if(m_pendingHits.begin(), m_pendingHits.end(), [](const PendingVisionHit &p) { return p.resolved; }),
        m_pendingHits.end());
    if (m_pendingHits.isEmpty() && m_visionPollTimer)
        m_visionPollTimer->stop();
}

void MatchRunningPage::onImuHitDetected(const QString &mac, double speedKmh, int powerTen, double, double, int durationMs,
                                      const QString &hitType, int strokeClassId, float strokeConfidence)
{
    if (!m_imuSubscribed)
        return;
    const int idx = playerIndexForMac(mac);
    if (idx < 0)
        return;

    m_playerStats[idx].lastImuSignalMs = QDateTime::currentMSecsSinceEpoch();
    if (!tryAcceptPlayerHit(m_playerStats[idx]))
        return;

    QString imuType = hitType.trimmed().isEmpty() ? QStringLiteral("挥拍") : hitType.trimmed();
    if (strokeConfidence >= 0.15f && strokeClassId >= 0 && hitType != QStringLiteral("挥拍"))
        imuType = hitType.trimmed();
    onPlayerImuHit(idx, speedKmh, powerTen, durationMs, imuType, strokeClassId, strokeConfidence);
}

void MatchRunningPage::subscribeImu()
{
    /* 与单人练习一致：摄像头高置信 / 摄像头挥拍 / 拍柄 IMU 三路 OR，按球员去重 */
        m_cameraHitSubscribed = true;
        m_imuSubscribed = true;
}

void MatchRunningPage::unsubscribeImu()
{
    m_imuSubscribed = false;
    m_cameraHitSubscribed = false;
    if (m_visionPollTimer)
        m_visionPollTimer->stop();
    m_pendingHits.clear();
}

void MatchRunningPage::enableCameraHitListen(bool on)
{
        m_cameraHitSubscribed = on;
    m_imuSubscribed = on;
}

void MatchRunningPage::startRunning()
{
    m_hits = 0;
    m_speedSum = 0;
    m_speedCount = 0;
    m_maxSpeed = 0;
    m_powerSum = 0;
    m_powerCount = 0;
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    m_endedAt = 0;
    m_hitRecords.clear();
    m_pendingHits.clear();
    m_prevYoloStable = false;
    m_prevYoloCls = -1;
    m_prevYoloScore = 0.0f;

    m_replaySessionId = QStringLiteral("match_%1").arg(QDateTime::currentMSecsSinceEpoch());
    publishReplaySession(m_replaySessionId);
    QDir().mkpath(replaySessionDir(m_replaySessionId));

    for (auto &ps : m_playerStats) {
        ps.hits = 0;
        ps.speedSum = 0;
        ps.speedCount = 0;
        ps.maxSpeed = 0;
        ps.powerSum = 0;
        ps.powerCount = 0;
        ps.scores.clear();
        ps.lastHitMs = 0;
        ps.lastImuSignalMs = 0;
        if (ps.hitCountLabel)
            ps.hitCountLabel->setText(QStringLiteral("0"));
        if (ps.speedLabel)
            ps.speedLabel->setText(QStringLiteral("--"));
        if (ps.powerLabel)
            ps.powerLabel->setText(QStringLiteral("--"));
        if (ps.avgSpeedLabel)
            ps.avgSpeedLabel->setText(QStringLiteral("--"));
        if (ps.actionTypeLabel)
            ps.actionTypeLabel->setText(QStringLiteral("--"));
        if (ps.aiScoreValue)
            ps.aiScoreValue->setText(QStringLiteral("--"));
        if (ps.adviceLabel)
            ps.adviceLabel->setText(QStringLiteral("完成击球后将显示智能教练建议。"));
        if (ps.statusHint)
            ps.statusHint->setText(QStringLiteral("比赛进行中，请开始击球…"));
    }

    if (m_mainWindow && m_mainWindow->sleImu())
        m_mainWindow->sleImu()->resetSwingDetector();
    if (m_mainWindow && m_mainWindow->yoloAction())
        m_mainWindow->yoloAction()->resetSessionBaseline();
    subscribeImu();
}

void MatchRunningPage::stopRunning()
{
    unsubscribeImu();
    m_endedAt = QDateTime::currentMSecsSinceEpoch();
    for (auto &p : m_pendingHits)
        finalizeHitVision(p);
    m_pendingHits.clear();
    /* 保留 m_replaySessionId 供报告页打开回放；仅停止 AI 继续采集 */
    clearReplaySession();
}

void MatchRunningPage::registerHitReplay(int hitIdx)
{
    if (m_replaySessionId.isEmpty() || hitIdx <= 0)
        return;
    requestHitReplayCapture(m_replaySessionId, hitIdx);
}

QList<MatchRunningPage::PlayerReportLine> MatchRunningPage::playerReportLines() const
{
    QList<PlayerReportLine> lines;
    for (const auto &ps : m_playerStats) {
        PlayerReportLine line;
        line.name = ps.binding.deviceName;
        line.hits = ps.hits;
        line.avgSpeed = ps.speedCount > 0 ? ps.speedSum / ps.speedCount : 0;
        line.avgScore = ps.scores.isEmpty()
            ? 0
            : std::accumulate(ps.scores.begin(), ps.scores.end(), 0) / ps.scores.size();
        lines.append(line);
    }
    return lines;
}

// ═══════════════════════════════════════════════
// 运动报告
// ═══════════════════════════════════════════════
MatchReportPage::MatchReportPage(MainWindow *mw, QWidget *parent)
    : PageBase(QStringLiteral("运动报告"), "", mw, parent)
{
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    connect(m_backBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(7); });
    m_backBtn->show();
    m_backBtn->setText(QStringLiteral("返回比赛"));
}

void MatchReportPage::showReport(MatchRunningPage *running)
{
    QLayoutItem *child;
    while (m_rootLayout->count() > 1) {
        child = m_rootLayout->takeAt(1);
        if (child->widget())
            delete child->widget();
        delete child;
    }

    m_replaySessionId = running ? running->replaySessionId() : QString();
    QList<MatchRunningPage::MatchHitRecord> records =
        running ? running->hitRecords() : QList<MatchRunningPage::MatchHitRecord>();

    /* —— 置顶回放区：不进滚动，保证第一眼可见 —— */
    auto *replayBar = new QFrame();
    replayBar->setObjectName(QStringLiteral("replayBar"));
    replayBar->setMinimumHeight(200);
    replayBar->setStyleSheet(QStringLiteral(
        "QFrame#replayBar {"
        "  border: 2px solid #56baff; border-radius: 14px;"
        "  background: #0d2848;"
        "  margin: 8px 16px 4px 16px;"
        "}"));
    auto *rbLay = new QVBoxLayout(replayBar);
    rbLay->setContentsMargins(14, 12, 14, 12);
    rbLay->setSpacing(10);

    auto *hitTitle = new QLabel(
        QStringLiteral("击球回放入口（共 %1 次）· 点下方卡片查看").arg(records.size()));
    hitTitle->setStyleSheet(
        QStringLiteral("font-size:24px; font-weight:800; color:#56baff; background:transparent;"));
    rbLay->addWidget(hitTitle);

    if (records.isEmpty()) {
        auto *empty = new QLabel(running && running->m_hits > 0
                                     ? QStringLiteral("统计有击球，但明细列表为空。请再打一场后查看。")
                                     : QStringLiteral("本场尚无击球记录。比赛中击球后，这里会出现可点卡片。"));
        empty->setWordWrap(true);
        empty->setStyleSheet(QStringLiteral("font-size:20px; color:#cfe2ff; background:transparent;"));
        rbLay->addWidget(empty);
    } else {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        const int showN = qMin(records.size(), 4);
        for (int i = 0; i < showN; ++i) {
            const auto &rec = records[i];
            const int hitIdx = rec.hitIdx > 0 ? rec.hitIdx : (i + 1);
            auto *card = new QPushButton(
                QStringLiteral("第%1次\n%2\n%3分\n查看回放")
                    .arg(hitIdx)
                    .arg(rec.actionType.trimmed().isEmpty() ? QStringLiteral("挥拍") : rec.actionType.trimmed())
                    .arg(rec.score));
            card->setMinimumSize(170, 140);
            card->setCursor(Qt::PointingHandCursor);
            card->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "  border: 2px solid #3d7fd4; border-radius: 12px;"
                "  background: #15406f; color: #eaf3ff;"
                "  font-size: 22px; font-weight: 700;"
                "}"
                "QPushButton:pressed { background: #1a5a9a; border-color: #56baff; }"));
            const int score = rec.score;
            const QString playerName = rec.playerName;
            const QString actionType = rec.actionType;
            const int speedKmh = rec.speedKmh;
            const int powerTen = rec.powerTen;
            const int durationMsHit = rec.durationMs;
            connect(card, &QPushButton::clicked, this,
                    [this, hitIdx, score, playerName, actionType, speedKmh, powerTen, durationMsHit]() {
                        emit actionClicked(hitIdx, score, playerName, actionType, speedKmh, powerTen, durationMsHit);
                    });
            row->addWidget(card);
        }
        if (records.size() > 4) {
            auto *more = new QLabel(QStringLiteral("…还有 %1 次\n请下滚查看").arg(records.size() - 4));
            more->setAlignment(Qt::AlignCenter);
            more->setStyleSheet(QStringLiteral("font-size:16px; color:#9eb7de; background:transparent;"));
            row->addWidget(more);
        }
        row->addStretch(1);
        rbLay->addLayout(row);
    }
    m_rootLayout->addWidget(replayBar, 0);

    /* —— 下方可滚动：总览 + 全部击球卡片 + 球员统计 —— */
    auto *pageScroll = new QScrollArea();
    pageScroll->setWidgetResizable(true);
    pageScroll->setFrameShape(QFrame::NoFrame);
    pageScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    pageScroll->setStyleSheet(QStringLiteral(
        "QScrollArea { border: none; background: transparent; }"
        "QScrollBar:vertical { width: 12px; background: #0a1a33; }"
        "QScrollBar::handle:vertical { background: #2e63ac; border-radius: 4px; min-height: 40px; }"));
    pageScroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    QScroller::grabGesture(pageScroll->viewport(), QScroller::TouchGesture);
    QScroller::grabGesture(pageScroll->viewport(), QScroller::LeftMouseButtonGesture);
    if (QScroller *sc = QScroller::scroller(pageScroll->viewport())) {
        QScrollerProperties sp = sc->scrollerProperties();
        sp.setScrollMetric(QScrollerProperties::FrameRate,
                           QVariant::fromValue(QScrollerProperties::Fps60));
        sp.setScrollMetric(QScrollerProperties::DragStartDistance, QVariant::fromValue<qreal>(0.002));
        sp.setScrollMetric(QScrollerProperties::DragVelocitySmoothingFactor,
                           QVariant::fromValue<qreal>(0.8));
        sp.setScrollMetric(QScrollerProperties::MinimumVelocity, QVariant::fromValue<qreal>(0.02));
        sp.setScrollMetric(QScrollerProperties::MaximumVelocity, QVariant::fromValue<qreal>(1.2));
        sp.setScrollMetric(QScrollerProperties::DecelerationFactor, QVariant::fromValue<qreal>(0.1));
        sp.setScrollMetric(QScrollerProperties::OvershootDragResistanceFactor,
                           QVariant::fromValue<qreal>(0.3));
        sc->setScrollerProperties(sp);
    }

    auto *content = new QWidget();
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto *wrap = new QVBoxLayout(content);
    wrap->setContentsMargins(24, 12, 24, 24);
    wrap->setSpacing(14);

    auto *head = new QFrame();
    head->setObjectName(QStringLiteral("card"));
    head->setStyleSheet(
        QStringLiteral("QFrame#card { border: 1px solid #234f8c; border-radius: 16px; background: #0a1a33c7; }"));
    auto *hdLay = new QHBoxLayout(head);
    hdLay->setContentsMargins(16, 14, 16, 14);
    auto *hdTitle = new QLabel(QStringLiteral("本次对打 / 比赛报告"));
    hdTitle->setStyleSheet(
        QStringLiteral("font-size:26px; font-weight:800; background:transparent; color:#eaf3ff;"));
    hdLay->addWidget(hdTitle);
    hdLay->addStretch();
    auto *badge = new QLabel(QStringLiteral("共 %1 次击球").arg(running ? running->m_hits : 0));
    badge->setStyleSheet(
        QStringLiteral("font-size:16px; font-weight:700; color:#cfe2ff; border:1px solid #2b5aa0; "
                       "border-radius:999px; padding:8px 16px; background:#07172f7a;"));
    hdLay->addWidget(badge);
    wrap->addWidget(head);

    const qint64 durationMs = std::max<qint64>(
        0, (running->m_endedAt ? running->m_endedAt : QDateTime::currentMSecsSinceEpoch()) - running->m_startedAt);
    const int durationSec = static_cast<int>(durationMs / 1000);
    const int avgSpeed = running->m_speedCount > 0 ? running->m_speedSum / running->m_speedCount : 0;
    const QString avgPower = running->m_powerCount > 0
        ? QString::number(running->m_powerSum * 1.0 / running->m_powerCount, 'f', 1)
        : QStringLiteral("--");

    auto *grid = new QGridLayout();
    grid->setSpacing(10);
    struct CardData {
        QString k;
        QString v;
    };
    const CardData cards[] = {
        {QStringLiteral("总击球次数"), QStringLiteral("%1 次").arg(running->m_hits)},
        {QStringLiteral("比赛时长"), QStringLiteral("%1 s").arg(durationSec)},
        {QStringLiteral("平均球速"), QStringLiteral("%1 km/h").arg(avgSpeed > 0 ? avgSpeed : 0)},
        {QStringLiteral("最高球速"), QStringLiteral("%1 km/h").arg(running->m_maxSpeed > 0 ? running->m_maxSpeed : 0)},
        {QStringLiteral("平均力度"), QStringLiteral("%1 /10").arg(avgPower)},
        {QStringLiteral("参赛人数"), QStringLiteral("%1 人").arg(running->m_players.size())},
    };
    for (int i = 0; i < 6; ++i) {
        auto *card = new QFrame();
        card->setObjectName(QStringLiteral("card"));
        card->setFixedHeight(90);
        card->setStyleSheet(
            QStringLiteral("QFrame#card { border: 1px solid #2e63ac; border-radius: 14px; "
                           "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445); }"));
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(12, 8, 12, 8);
        auto *kl = new QLabel(cards[i].k);
        kl->setStyleSheet(QStringLiteral("color:#8cc7ff; font-size:17px; background:transparent;"));
        cl->addWidget(kl);
        auto *vl = new QLabel(cards[i].v);
        vl->setStyleSheet(QStringLiteral("color:#eaf3ff; font-size:30px; font-weight:900; background:transparent;"));
        cl->addWidget(vl);
        grid->addWidget(card, i / 3, i % 3);
    }
    wrap->addLayout(grid);

    if (!records.isEmpty()) {
        auto *allTitle = new QLabel(QStringLiteral("全部击球记录"));
        allTitle->setStyleSheet(
            QStringLiteral("font-size:24px; font-weight:800; color:#8cc7ff; background:transparent;"));
        wrap->addWidget(allTitle);
        auto *scoreGrid = new QGridLayout();
        scoreGrid->setSpacing(12);
        for (int i = 0; i < records.size(); ++i) {
            const auto &rec = records[i];
            const int hitIdx = rec.hitIdx > 0 ? rec.hitIdx : (i + 1);
            auto *card = new QPushButton(
                QStringLiteral("第%1次 · %2\n%3 · %4分\n点击查看回放")
                    .arg(hitIdx)
                    .arg(rec.playerName)
                    .arg(rec.actionType.trimmed().isEmpty() ? QStringLiteral("挥拍") : rec.actionType.trimmed())
                    .arg(rec.score));
            card->setMinimumSize(220, 130);
            card->setCursor(Qt::PointingHandCursor);
            card->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "  border: 2px solid #3d7fd4; border-radius: 14px;"
                "  background: #15406f; color: #eaf3ff; font-size: 22px; font-weight: 700;"
                "}"
                "QPushButton:pressed { background: #1a5a9a; }"));
            const int score = rec.score;
            const QString playerName = rec.playerName;
            const QString actionType = rec.actionType;
            const int speedKmh = rec.speedKmh;
            const int powerTen = rec.powerTen;
            const int durationMsHit = rec.durationMs;
            connect(card, &QPushButton::clicked, this,
                    [this, hitIdx, score, playerName, actionType, speedKmh, powerTen, durationMsHit]() {
                        emit actionClicked(hitIdx, score, playerName, actionType, speedKmh, powerTen, durationMsHit);
                    });
            scoreGrid->addWidget(card, i / 4, i % 4);
        }
        wrap->addLayout(scoreGrid);
    }

    if (running && !running->playerReportLines().isEmpty()) {
        auto *playerTitle = new QLabel(QStringLiteral("各球员统计"));
        playerTitle->setStyleSheet(
            QStringLiteral("font-size:24px; font-weight:800; color:#8cc7ff; background:transparent;"));
        wrap->addWidget(playerTitle);
        for (const auto &line : running->playerReportLines()) {
            auto *row = new QLabel(QStringLiteral("%1 · 击球 %2 次 · 均分 %3 · 均速 %4 km/h")
                                       .arg(line.name)
                                       .arg(line.hits)
                                       .arg(line.hits > 0 ? QString::number(line.avgScore) : QStringLiteral("--"))
                                       .arg(line.avgSpeed > 0 ? line.avgSpeed : 0));
            row->setStyleSheet(QStringLiteral("font-size:22px; color:#d9e9ff; background:transparent;"));
            wrap->addWidget(row);
        }
    }

    wrap->addStretch(1);
    pageScroll->setWidget(content);
    m_rootLayout->addWidget(pageScroll, 1);
}
