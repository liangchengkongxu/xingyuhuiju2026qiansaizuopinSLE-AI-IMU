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
#include <numeric>
#include <algorithm>


#include "ui_common.h"
#include "ui_pages.h"
#include "main_window.h"

// ═══════════════════════════════════════════════
// 多人/班级模式 实现
// ═══════════════════════════════════════════════
MultiPage::MultiPage(MainWindow *mw, QWidget *parent)
    : PageBase(QStringLiteral("班级模式"), "", mw, parent), m_deviceScroll(nullptr), m_deviceGridCols(0)
{
    connect(m_backBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();

    auto *body = new QVBoxLayout();
    body->setContentsMargins(24, 24, 24, 24);
    body->setSpacing(16);
    body->addStretch();

    // 操作行
    auto *actionsRow = new QHBoxLayout();
    actionsRow->setSpacing(12);
    actionsRow->addStretch();

    m_scanBtn = new QPushButton("扫描设备");
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
    connect(m_scanBtn, &QPushButton::clicked, this, &MultiPage::beginScan);
    actionsRow->addWidget(m_scanBtn);

    m_nextBtn = makePrimaryBtn("下一步");
    m_nextBtn->setFixedSize(200, 96);
    m_nextBtn->setStyleSheet(m_nextBtn->styleSheet() + "font-size: 22px;");
    m_nextBtn->setEnabled(false);
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        if (m_deviceCodes.isEmpty()) {
            m_scanMsg->setText(QStringLiteral("请先完成设备扫描"));
            return;
        }
        if (m_mainWindow && m_mainWindow->sleSeek() && m_mainWindow->sleSeek()->isScanning())
            m_mainWindow->sleSeek()->stopScan();
        emit openClassTrainNoGroup();
    });
    actionsRow->addWidget(m_nextBtn);

    actionsRow->addStretch();
    body->addLayout(actionsRow);

    m_scanMsg = new QLabel("按下后将扫描拍柄星闪广播（约 6–8 秒）。");
    m_scanMsg->setStyleSheet("font-size:20px; color:#9eb7de; background:transparent;");
    m_scanMsg->setAlignment(Qt::AlignCenter);
    body->addWidget(m_scanMsg);

    // 扫描结果区
    auto *resultsFrame = new QFrame();
    resultsFrame->setObjectName("card");
    resultsFrame->setStyleSheet("QFrame#card { border: 1px solid #234f8c; border-radius: 18px; background: #07172fb0; }");
    auto *rfLay = new QVBoxLayout(resultsFrame);
    rfLay->setContentsMargins(12, 12, 12, 12);
    rfLay->setSpacing(10);

    auto *rfHead = new QHBoxLayout();
    auto *rfTitle = new QLabel("扫描到的星闪设备");
    rfTitle->setStyleSheet("color:#8cc7ff; font-weight:800; background:transparent; font-size:24px;");
    rfHead->addWidget(rfTitle);
    rfHead->addStretch();
    m_scanCount = new QLabel("0 台");
    m_scanCount->setStyleSheet("color:#9eb7de; font-size:22px; background:transparent;");
    rfHead->addWidget(m_scanCount);
    rfLay->addLayout(rfHead);

    m_deviceScroll = new QScrollArea();
    m_deviceScroll->setWidgetResizable(true);
    m_deviceScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    m_deviceScroll->viewport()->installEventFilter(this);
    m_deviceGrid = new QWidget();
    m_deviceGrid->setStyleSheet("background:transparent;");
    auto *devGridLay = new QGridLayout();
    devGridLay->setContentsMargins(4, 4, 4, 4);
    devGridLay->setHorizontalSpacing(18);
    devGridLay->setVerticalSpacing(18);
    m_deviceGrid->setLayout(devGridLay);
    m_deviceScroll->setWidget(m_deviceGrid);
    rfLay->addWidget(m_deviceScroll, 1);

    body->addWidget(resultsFrame, 1);
    body->addStretch();
    m_rootLayout->addLayout(body);
}

int MultiPage::computeDeviceGridCols() const
{
    if (!m_deviceScroll)
        return 2;
    int vw = m_deviceScroll->viewport()->width();
    if (vw < 80)
        vw = 900;
    const int minCellW = 440;
    const int sp = 16;
    const int cols = qMax(1, (vw + sp) / (minCellW + sp));
    return qBound(1, cols, 3);
}

int MultiPage::computeDeviceCellWidth() const
{
    if (!m_deviceScroll)
        return 440;
    int vw = m_deviceScroll->viewport()->width();
    if (vw < 80)
        vw = 900;
    const int cols = computeDeviceGridCols();
    const int sp = 16;
    return qMax(400, (vw - (cols + 1) * sp) / cols);
}

void MultiPage::rebuildDeviceGrid()
{
    auto *grid = qobject_cast<QGridLayout *>(m_deviceGrid->layout());
    if (!grid)
        return;

    QLayoutItem *child;
    while ((child = grid->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }

    const int n = m_seekDevices.isEmpty() ? m_deviceCodes.size() : m_seekDevices.size();
    if (n == 0) {
        m_deviceGridCols = 0;
        return;
    }

    const int cols = computeDeviceGridCols();
    const int cellW = computeDeviceCellWidth();
    m_deviceGridCols = cols;

    auto addLine = [](QVBoxLayout *lay, const QString &label, const QString &value, int valuePx = 26) {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        auto *kl = new QLabel(label);
        kl->setStyleSheet("font-size:22px; color:#8cc7ff; background:transparent; font-weight:700;");
        kl->setFixedWidth(80);
        kl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto *vl = new QLabel(value);
        vl->setStyleSheet(QStringLiteral(
            "font-size:%1px; color:#eaf3ff; background:transparent; font-weight:800;").arg(valuePx));
        vl->setWordWrap(true);
        vl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        row->addWidget(kl);
        row->addWidget(vl, 1);
        lay->addLayout(row);
    };

    for (int i = 0; i < n; ++i) {
        QString devNo = QStringLiteral("--");
        QString mac = QStringLiteral("-");
        QString name = QStringLiteral("-");
        QString rssi = QStringLiteral("-");
        QString level;
        QString power;

        if (!m_seekDevices.isEmpty()) {
            const SleSeekDevice &d = m_seekDevices.at(i);
            mac = d.mac;
            name = d.name.isEmpty() ? QStringLiteral("-") : d.name;
            rssi = QStringLiteral("%1 dBm").arg(d.rssi);
            if (d.hasDiscoveryLevel)
                level = QString::number(d.discoveryLevel);
            if (d.hasTxPower)
                power = QStringLiteral("%1 dBm").arg(d.txPowerDbm);
            devNo = d.deviceId > 0 ? QString::number(d.deviceId) : QStringLiteral("--");
        } else if (i < m_deviceCodes.size()) {
            devNo = m_deviceCodes.at(i);
        }

        auto *card = new QFrame();
        card->setObjectName("dcard");
        card->setFixedWidth(cellW);
        card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        card->setStyleSheet(
            "QFrame#dcard { border: 2px solid #3a7fd4; border-radius: 18px; "
            "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445); }");
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(20, 16, 20, 16);
        cl->setSpacing(10);
        auto *title = new QLabel(QStringLiteral("设备 %1").arg(devNo));
        title->setStyleSheet("font-size:38px; font-weight:900; color:#ffffff; background:transparent;");
        title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        cl->addWidget(title);
        addLine(cl, QStringLiteral("MAC"), mac, 28);
        addLine(cl, QStringLiteral("名称"), name, 26);
        addLine(cl, QStringLiteral("信号"), rssi, 26);
        if (!level.isEmpty())
            addLine(cl, QStringLiteral("等级"), level, 24);
        if (!power.isEmpty())
            addLine(cl, QStringLiteral("功率"), power, 24);
        grid->addWidget(card, i / cols, i % cols, Qt::AlignLeft | Qt::AlignTop);
    }

    for (int c = 0; c < cols; ++c)
        grid->setColumnStretch(c, 0);
    grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
}

bool MultiPage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_deviceScroll->viewport() && event->type() == QEvent::Resize) {
        if (!m_seekDevices.isEmpty() || !m_deviceCodes.isEmpty()) {
            const int want = computeDeviceGridCols();
            if (want != m_deviceGridCols)
                rebuildDeviceGrid();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MultiPage::beginScan()
{
    if (!m_mainWindow || !m_mainWindow->sleSeek())
        return;
    m_seekDevices.clear();
    m_deviceCodes.clear();
    m_deviceGridCols = 0;
    if (m_scanCount)
        m_scanCount->setText(QStringLiteral("0 台"));
    QLayoutItem *child;
    while (m_deviceGrid && m_deviceGrid->layout()
        && (child = m_deviceGrid->layout()->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    updateNextButton();
    m_scanBtn->setEnabled(false);
    m_scanMsg->setText(QStringLiteral("扫描中…"));
    m_mainWindow->sleSeek()->startScan();
}

void MultiPage::updateNextButton()
{
    if (!m_nextBtn)
        return;
    m_nextBtn->setVisible(true);
    m_nextBtn->setEnabled(!m_deviceCodes.isEmpty());
}

void MultiPage::onSeekStatus(const QString &msg)
{
    if (!msg.isEmpty())
        m_scanMsg->setText(msg);
}

void MultiPage::applyScanResults(const QList<SleSeekDevice> &devices)
{
    m_seekDevices = devices;
    m_deviceCodes.clear();
    for (const SleSeekDevice &d : devices) {
        if (d.deviceId > 0)
            m_deviceCodes.append(QString::number(d.deviceId));
        else
            m_deviceCodes.append(d.mac);
    }
    m_scanCount->setText(QStringLiteral("%1 台").arg(devices.size()));
    rebuildDeviceGrid();
    updateNextButton();
    QTimer::singleShot(0, this, [this]() {
        if (m_seekDevices.isEmpty() && m_deviceCodes.isEmpty())
            return;
        const int want = computeDeviceGridCols();
        if (want != m_deviceGridCols)
            rebuildDeviceGrid();
    });
}

void MultiPage::afterScanUiReady()
{
    QTimer::singleShot(0, this, [this]() {
        if (m_seekDevices.isEmpty() && m_deviceCodes.isEmpty())
            return;
        const int want = computeDeviceGridCols();
        if (want != m_deviceGridCols)
            rebuildDeviceGrid();
    });
    m_nextBtn->show();
    updateNextButton();
    m_scanBtn->setEnabled(true);
}

void MultiPage::resetScan() {
    m_deviceCodes.clear();
    m_seekDevices.clear();
    m_deviceGridCols = 0;
    m_scanMsg->setText(QStringLiteral("按下后将扫描拍柄星闪广播（约 6–8 秒）。"));
    m_scanCount->setText("0 台");
    updateNextButton();
    QLayoutItem *child;
    while ((child = m_deviceGrid->layout()->takeAt(0)) != nullptr) {
        delete child->widget(); delete child;
    }
}

// ═══════════════════════════════════════════════
// 单人练习：扫描 + 确认 IMU 来源
// ═══════════════════════════════════════════════
SinglePracticeSetupPage::SinglePracticeSetupPage(MainWindow *mw, QWidget *parent)
    : PageBase(QStringLiteral("单人练习 · 设备绑定"), "", mw, parent)
{
    connect(m_backBtn, &QPushButton::clicked, this, [this, mw]() {
        showScanStep();
        mw->switchPage(1);
    });
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();

    m_stepStack = new QStackedWidget();
    m_stepStack->setStyleSheet("background: transparent;");

    // ── 步骤 1：扫描（布局同班级模式）──
    m_scanStep = new QWidget();
    m_scanStep->setStyleSheet("background: transparent;");
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
    connect(m_scanBtn, &QPushButton::clicked, this, &SinglePracticeSetupPage::beginScan);
    scanActions->addWidget(m_scanBtn);

    m_scanNextBtn = makePrimaryBtn(QStringLiteral("下一步"));
    m_scanNextBtn->setFixedSize(240, 96);
    m_scanNextBtn->setStyleSheet(m_scanNextBtn->styleSheet() + "font-size: 22px;");
    m_scanNextBtn->setEnabled(false);
    connect(m_scanNextBtn, &QPushButton::clicked, this, &SinglePracticeSetupPage::goConfirmStep);
    scanActions->addWidget(m_scanNextBtn);
    scanActions->addStretch();
    scanBody->addLayout(scanActions);

    m_scanMsg = new QLabel(QStringLiteral("请先扫描并绑定练习用拍柄（约 6–8 秒）。"));
    m_scanMsg->setStyleSheet("font-size:20px; color:#9eb7de; background:transparent;");
    m_scanMsg->setAlignment(Qt::AlignCenter);
    m_scanMsg->setWordWrap(true);
    scanBody->addWidget(m_scanMsg);

    auto *resultsFrame = new QFrame();
    resultsFrame->setObjectName("card");
    resultsFrame->setStyleSheet(
        "QFrame#card { border: 1px solid #234f8c; border-radius: 18px; background: #07172fb0; }");
    auto *rfLay = new QVBoxLayout(resultsFrame);
    rfLay->setContentsMargins(12, 12, 12, 12);
    auto *rfHead = new QHBoxLayout();
    auto *rfTitle = new QLabel(QStringLiteral("扫描到的星闪设备"));
    rfTitle->setStyleSheet("color:#8cc7ff; font-weight:800; background:transparent; font-size:24px;");
    rfHead->addWidget(rfTitle);
    rfHead->addStretch();
    m_scanCount = new QLabel(QStringLiteral("0 台"));
    m_scanCount->setStyleSheet("color:#9eb7de; font-size:22px; background:transparent;");
    rfHead->addWidget(m_scanCount);
    rfLay->addLayout(rfHead);

    m_deviceScroll = new QScrollArea();
    m_deviceScroll->setWidgetResizable(true);
    m_deviceScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    m_deviceScroll->viewport()->setStyleSheet("background: transparent;");
    m_deviceScroll->viewport()->installEventFilter(this);
    m_deviceGrid = new QWidget();
    m_deviceGrid->setStyleSheet("background: transparent;");
    auto *devGridLay = new QGridLayout();
    devGridLay->setContentsMargins(4, 4, 4, 4);
    devGridLay->setHorizontalSpacing(18);
    devGridLay->setVerticalSpacing(18);
    m_deviceGrid->setLayout(devGridLay);
    m_deviceScroll->setWidget(m_deviceGrid);
    rfLay->addWidget(m_deviceScroll, 1);
    scanBody->addWidget(resultsFrame, 1);

    m_stepStack->addWidget(m_scanStep);

    // ── 步骤 2：确认传感器数据来源 ──
    m_confirmStep = new QWidget();
    m_confirmStep->setStyleSheet("background: transparent;");
    auto *confBody = new QVBoxLayout(m_confirmStep);
    confBody->setContentsMargins(24, 16, 24, 16);
    confBody->setSpacing(14);

    m_confirmHint = new QLabel(QStringLiteral("请选择单人练习使用的拍柄，确认后进入动作练习。"));
    m_confirmHint->setStyleSheet("font-size:20px; color:#cfe2ff; background:transparent;");
    m_confirmHint->setAlignment(Qt::AlignCenter);
    m_confirmHint->setWordWrap(true);
    confBody->addWidget(m_confirmHint);

    m_sourceLabel = new QLabel(QStringLiteral("数据来源：拍柄星闪广播 IMU（上电即广播，无需连接发令）"));
    m_sourceLabel->setStyleSheet(
        "font-size:18px; color:#8cc7ff; background:#07172f99; border:1px solid #2e63ac; "
        "border-radius:12px; padding:12px 16px;");
    m_sourceLabel->setWordWrap(true);
    confBody->addWidget(m_sourceLabel);

    m_confirmScroll = new QScrollArea();
    m_confirmScroll->setWidgetResizable(true);
    m_confirmScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    m_confirmList = new QWidget();
    m_confirmList->setLayout(new QVBoxLayout());
    qobject_cast<QVBoxLayout *>(m_confirmList->layout())->setSpacing(12);
    m_confirmScroll->setWidget(m_confirmList);
    confBody->addWidget(m_confirmScroll, 1);

    auto *confActions = new QHBoxLayout();
    confActions->addStretch();
    auto *backScanBtn = makeGhostBtn(QStringLiteral("重新扫描"));
    backScanBtn->setFixedHeight(72);
    connect(backScanBtn, &QPushButton::clicked, this, [this]() { showScanStep(); });
    confActions->addWidget(backScanBtn);

    m_confirmBtn = makePrimaryBtn(QStringLiteral("确认 · 进入单人练习"));
    m_confirmBtn->setFixedSize(360, 88);
    m_confirmBtn->setStyleSheet(m_confirmBtn->styleSheet() + "font-size: 20px;");
    m_confirmBtn->setEnabled(false);
    connect(m_confirmBtn, &QPushButton::clicked, this, &SinglePracticeSetupPage::confirmAndEnterPractice);
    confActions->addWidget(m_confirmBtn);
    confActions->addStretch();
    confBody->addLayout(confActions);

    m_stepStack->addWidget(m_confirmStep);
    m_rootLayout->addWidget(m_stepStack, 1);
    showScanStep();
}

void SinglePracticeSetupPage::showScanStep()
{
    if (m_stepStack)
        m_stepStack->setCurrentWidget(m_scanStep);
    m_titleLabel->setText(QStringLiteral("单人练习 · 扫描拍柄"));
    if (m_backBtn && m_mainWindow) {
        disconnect(m_backBtn, nullptr, nullptr, nullptr);
        connect(m_backBtn, &QPushButton::clicked, m_mainWindow, [mw = m_mainWindow]() { mw->switchPage(1); });
    }
}

void SinglePracticeSetupPage::showConfirmStep()
{
    if (m_seekDevices.isEmpty()) {
        m_scanMsg->setText(QStringLiteral("请先完成设备扫描"));
        showScanStep();
        return;
    }
    rebuildConfirmList();
    if (m_stepStack)
        m_stepStack->setCurrentWidget(m_confirmStep);
    m_titleLabel->setText(QStringLiteral("单人练习 · 确认数据来源"));
    if (m_backBtn) {
        disconnect(m_backBtn, nullptr, nullptr, nullptr);
        connect(m_backBtn, &QPushButton::clicked, this, [this]() { showScanStep(); });
    }
}

int SinglePracticeSetupPage::computeDeviceGridCols() const
{
    if (!m_deviceScroll)
        return 2;
    int vw = m_deviceScroll->viewport()->width();
    if (vw < 80)
        vw = 900;
    const int minCellW = 440;
    const int sp = 16;
    return qBound(1, qMax(1, (vw + sp) / (minCellW + sp)), 3);
}

int SinglePracticeSetupPage::computeDeviceCellWidth() const
{
    if (!m_deviceScroll)
        return 440;
    int vw = m_deviceScroll->viewport()->width();
    if (vw < 80)
        vw = 900;
    const int cols = computeDeviceGridCols();
    const int sp = 16;
    return qMax(400, (vw - (cols + 1) * sp) / cols);
}

void SinglePracticeSetupPage::rebuildDeviceGrid()
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

    auto addLine = [](QVBoxLayout *lay, const QString &label, const QString &value, int valuePx = 26) {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        auto *kl = new QLabel(label);
        kl->setStyleSheet("font-size:22px; color:#8cc7ff; background:transparent; font-weight:700;");
        kl->setFixedWidth(80);
        kl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        auto *vl = new QLabel(value);
        vl->setStyleSheet(QStringLiteral(
            "font-size:%1px; color:#eaf3ff; background:transparent; font-weight:800;").arg(valuePx));
        vl->setWordWrap(true);
        vl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        row->addWidget(kl);
        row->addWidget(vl, 1);
        lay->addLayout(row);
    };

    for (int i = 0; i < n; ++i) {
        const SleSeekDevice &d = m_seekDevices.at(i);
        const QString devNo = d.deviceId > 0 ? QString::number(d.deviceId) : QStringLiteral("--");
        QString level;
        QString power;
        if (d.hasDiscoveryLevel)
            level = QString::number(d.discoveryLevel);
        if (d.hasTxPower)
            power = QStringLiteral("%1 dBm").arg(d.txPowerDbm);

        auto *card = new QFrame();
        card->setObjectName("dcard");
        card->setFixedWidth(cellW);
        card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Minimum);
        card->setAttribute(Qt::WA_StyledBackground, true);
        card->setStyleSheet(
            "QFrame#dcard { border: 2px solid #3a7fd4; border-radius: 18px; "
            "background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445); }");
        auto *cl = new QVBoxLayout(card);
        cl->setContentsMargins(20, 16, 20, 16);
        cl->setSpacing(10);
        auto *title = new QLabel(QStringLiteral("设备 %1").arg(devNo));
        title->setStyleSheet("font-size:38px; font-weight:900; color:#ffffff; background:transparent;");
        title->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        cl->addWidget(title);
        addLine(cl, QStringLiteral("MAC"), d.mac, 28);
        addLine(cl, QStringLiteral("名称"), d.name.isEmpty() ? QStringLiteral("-") : d.name, 26);
        addLine(cl, QStringLiteral("信号"), QStringLiteral("%1 dBm").arg(d.rssi), 26);
        if (!level.isEmpty())
            addLine(cl, QStringLiteral("等级"), level, 24);
        if (!power.isEmpty())
            addLine(cl, QStringLiteral("功率"), power, 24);
        grid->addWidget(card, i / cols, i % cols, Qt::AlignLeft | Qt::AlignTop);
    }

    for (int c = 0; c < cols; ++c)
        grid->setColumnStretch(c, 0);
    grid->setAlignment(Qt::AlignLeft | Qt::AlignTop);
}

void SinglePracticeSetupPage::rebuildConfirmList()
{
    auto *lay = qobject_cast<QVBoxLayout *>(m_confirmList->layout());
    if (!lay)
        return;
    QLayoutItem *child;
    while ((child = lay->takeAt(0)) != nullptr) {
        delete child->widget();
        delete child;
    }
    m_selectedIndex = m_seekDevices.isEmpty() ? -1 : 0;

    for (int i = 0; i < m_seekDevices.size(); ++i) {
        const SleSeekDevice &d = m_seekDevices.at(i);
        const QString devNo = d.deviceId > 0 ? QString::number(d.deviceId) : QStringLiteral("--");
        auto *row = new QPushButton();
        row->setCheckable(true);
        row->setCursor(Qt::PointingHandCursor);
        row->setMinimumHeight(88);
        row->setText(QStringLiteral("设备 %1 · %2 · %3 · 信号 %4 dBm")
                         .arg(devNo)
                         .arg(d.name.isEmpty() ? QStringLiteral("未命名") : d.name)
                         .arg(d.mac)
                         .arg(d.rssi));
        row->setStyleSheet(R"(
            QPushButton {
                border: 2px solid #2e63ac; border-radius: 14px;
                background: #0e2445; color: #eaf3ff; font-size: 18px; font-weight: 700;
                text-align: left; padding: 14px 18px;
            }
            QPushButton:checked {
                border-color: #56baff; background: #153a6f;
            }
            QPushButton:hover { border-color: #56baff; }
        )");
        const int idx = i;
        connect(row, &QPushButton::clicked, this, [this, idx, row]() {
            m_selectedIndex = idx;
            for (int j = 0; j < m_confirmList->layout()->count(); ++j) {
                if (auto *b = qobject_cast<QPushButton *>(m_confirmList->layout()->itemAt(j)->widget()))
                    b->setChecked(j == idx);
            }
            if (m_confirmBtn)
                m_confirmBtn->setEnabled(true);
        });
        if (i == m_selectedIndex) {
            row->setChecked(true);
            if (m_confirmBtn)
                m_confirmBtn->setEnabled(true);
        }
        lay->addWidget(row);
    }
    lay->addStretch();
}

bool SinglePracticeSetupPage::eventFilter(QObject *watched, QEvent *event)
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

void SinglePracticeSetupPage::beginScan()
{
    if (!m_mainWindow || !m_mainWindow->sleSeek())
        return;
    m_seekDevices.clear();
    m_selectedIndex = -1;
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
    m_scanMsg->setText(QStringLiteral("扫描中…"));
    m_mainWindow->sleSeek()->startScan();
}

void SinglePracticeSetupPage::updateScanNextButton()
{
    if (!m_scanNextBtn)
        return;
    m_scanNextBtn->setVisible(true);
    m_scanNextBtn->setEnabled(!m_seekDevices.isEmpty());
}

void SinglePracticeSetupPage::goConfirmStep()
{
    if (m_seekDevices.isEmpty()) {
        m_scanMsg->setText(QStringLiteral("请先完成设备扫描"));
        return;
    }
    if (m_mainWindow && m_mainWindow->sleSeek() && m_mainWindow->sleSeek()->isScanning())
        m_mainWindow->sleSeek()->stopScan();
    showConfirmStep();
}

void SinglePracticeSetupPage::confirmAndEnterPractice()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_seekDevices.size())
        return;
    const SleSeekDevice &d = m_seekDevices.at(m_selectedIndex);
    const QString code = d.deviceId > 0 ? QString::number(d.deviceId) : d.mac;
    emit deviceConfirmed(d.deviceId > 0 ? d.deviceId : 0, code,
                         d.name.isEmpty() ? QStringLiteral("拍柄") : d.name, d.mac);
}

void SinglePracticeSetupPage::onSeekStatus(const QString &msg)
{
    if (!msg.isEmpty() && m_scanMsg)
        m_scanMsg->setText(msg);
}

void SinglePracticeSetupPage::applyScanResults(const QList<SleSeekDevice> &devices)
{
    m_seekDevices = devices;
    if (m_scanCount)
        m_scanCount->setText(QStringLiteral("%1 台").arg(devices.size()));
    rebuildDeviceGrid();
    updateScanNextButton();
}

void SinglePracticeSetupPage::afterScanUiReady()
{
    if (m_scanBtn)
        m_scanBtn->setEnabled(true);
    updateScanNextButton();
}

void SinglePracticeSetupPage::resetScan()
{
    m_seekDevices.clear();
    m_selectedIndex = -1;
    m_deviceGridCols = 0;
    if (m_scanMsg)
        m_scanMsg->setText(QStringLiteral("请先扫描并绑定练习用拍柄（约 6–8 秒）。"));
    if (m_scanCount)
        m_scanCount->setText(QStringLiteral("0 台"));
    updateScanNextButton();
    if (m_confirmBtn)
        m_confirmBtn->setEnabled(false);
    if (m_deviceGrid && m_deviceGrid->layout()) {
        QLayoutItem *child;
        while ((child = m_deviceGrid->layout()->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
    }
    if (m_confirmList && m_confirmList->layout()) {
        QLayoutItem *child;
        while ((child = m_confirmList->layout()->takeAt(0)) != nullptr) {
            delete child->widget();
            delete child;
        }
    }
    showScanStep();
}

// ═══════════════════════════════════════════════
// 设置分组 实现
// ═══════════════════════════════════════════════
GroupPage::GroupPage(MainWindow *mw, QWidget *parent)
    : PageBase("设置分组", "", mw, parent), m_nextBucketSeq(1)
{
    connect(m_backBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(10); });
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();
    m_backBtn->setText("返回扫描");

    auto *body = new QVBoxLayout();
    body->setContentsMargins(24, 16, 24, 16);
    body->setSpacing(12);

    auto *hint = new QLabel("可将设备拖拽/点选加入分组（占位简化版：点击设备选中，再点分组加入；组内✕移回未分配）。");
    hint->setStyleSheet("font-size:13px; color:#9eb7de; background:transparent;");
    hint->setWordWrap(true);
    body->addWidget(hint);

    auto *toolbar = new QHBoxLayout();
    auto *addBtn = makePrimaryBtn("新建分组");
    connect(addBtn, &QPushButton::clicked, this, [this]() {
        if (m_buckets.size() >= 8) return;
        m_buckets.append({QString("b%1").arg(m_nextBucketSeq++), {}});
        render();
    });
    toolbar->addWidget(addBtn);
    toolbar->addStretch();
    m_toolbarMeta = new QLabel("");
    m_toolbarMeta->setStyleSheet("font-size:12px; color:#8cc7ff; background:transparent;");
    toolbar->addWidget(m_toolbarMeta);
    body->addLayout(toolbar);

    // 分组布局: 左-未分配 右-分组buckets
    auto *gridLayout = new QHBoxLayout();
    gridLayout->setSpacing(14);

    auto *poolFrame = new QFrame();
    poolFrame->setObjectName("card");
    poolFrame->setStyleSheet("QFrame#card { border: 1px solid #234f8c; border-radius: 16px; background: #07172fb0; }");
    auto *pfLay = new QVBoxLayout(poolFrame);
    pfLay->setContentsMargins(12, 12, 12, 12);
    auto *pfTitle = new QLabel("未分配设备");
    pfTitle->setStyleSheet("color:#8cc7ff; font-size:14px; font-weight:800; background:transparent;");
    pfLay->addWidget(pfTitle);
    auto *poolScroll = new QScrollArea();
    poolScroll->setWidgetResizable(true);
    poolScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    m_unassignedChips = new QWidget();
    m_unassignedChips->setStyleSheet("background:transparent;");
    m_unassignedChips->setLayout(new QVBoxLayout());
    ((QVBoxLayout*)m_unassignedChips->layout())->setSpacing(6);
    poolScroll->setWidget(m_unassignedChips);
    pfLay->addWidget(poolScroll, 1);
    gridLayout->addWidget(poolFrame, 1);

    auto *bucketsScroll = new QScrollArea();
    bucketsScroll->setWidgetResizable(true);
    bucketsScroll->setStyleSheet("QScrollArea { border: none; background: transparent; }");
    auto *bucketsFrame = new QFrame();
    bucketsFrame->setObjectName("card");
    bucketsFrame->setStyleSheet("QFrame#card { border: 1px solid #234f8c; border-radius: 16px; background: #07172f88; }");
    m_bucketsEl = new QWidget();
    m_bucketsEl->setStyleSheet("background:transparent;");
    m_bucketsEl->setLayout(new QGridLayout());
    ((QGridLayout*)m_bucketsEl->layout())->setSpacing(10);
    auto *bfLay = new QVBoxLayout(bucketsFrame);
    bfLay->setContentsMargins(10, 10, 10, 10);
    bfLay->addWidget(m_bucketsEl);
    bucketsScroll->setWidget(bucketsFrame);
    gridLayout->addWidget(bucketsScroll, 2);

    body->addLayout(gridLayout, 1);

    // 保存并继续
    auto *saveBtn = new QPushButton("保存分组并继续");
    saveBtn->setCursor(Qt::PointingHandCursor);
    saveBtn->setFixedHeight(66);
    saveBtn->setStyleSheet(R"(
        QPushButton {
            border: 1px solid #2f6ab4; border-radius: 16px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #1d5aa1, stop:1 #123a6f);
            color: #eaf3ff; font-size: 22px; font-weight: 900;
        }
        QPushButton:hover { border-color: #56baff; }
    )");
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        emit savedAndContinue();
    });
    body->addWidget(saveBtn, 0, Qt::AlignCenter);

    m_rootLayout->addLayout(body);
}

void GroupPage::openEditor(const QStringList &deviceCodes) {
    m_unassigned = deviceCodes;
    m_nextBucketSeq = 1;
    m_buckets.clear();
    m_buckets.append({QString("b%1").arg(m_nextBucketSeq++), {}});
    m_selectedCode.clear();
    render();
}

void GroupPage::render() {
    // 未分配 chips
    QLayoutItem *child;
    QVBoxLayout *ucl = (QVBoxLayout*)m_unassignedChips->layout();
    while ((child = ucl->takeAt(0)) != nullptr) { delete child->widget(); delete child; }
    for (const auto &code : m_unassigned) {
        auto *chip = new QPushButton(code);
        chip->setCursor(Qt::PointingHandCursor);
        chip->setStyleSheet(QString(R"(
            QPushButton {
                border: 1px solid %1; border-radius: 10px;
                background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445);
                color: #eaf3ff; font-size: 13px; font-weight: 800; padding: 10px 12px;
                text-align: left;
            }
            QPushButton:hover { border-color: #56baff; }
        )").arg(m_selectedCode == code ? "#ffcf66" : "#3065ab"));
        connect(chip, &QPushButton::clicked, this, [this, code]() {
            m_selectedCode = (m_selectedCode == code) ? QString() : code;
            render();
        });
        ucl->addWidget(chip);
    }

    // Buckets
    QGridLayout *bgl = (QGridLayout*)m_bucketsEl->layout();
    while ((child = bgl->takeAt(0)) != nullptr) { delete child->widget(); delete child; }
    for (int i = 0; i < m_buckets.size(); ++i) {
        const auto &b = m_buckets[i];
        auto *bucket = new QFrame();
        bucket->setObjectName("card");
        bucket->setMinimumSize(160, 120);
        bucket->setStyleSheet("QFrame#card { border: 1px solid #2e63ac; border-radius: 14px; background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #0a1a33, stop:1 #08162d); }");
        auto *bl = new QVBoxLayout(bucket);
        bl->setContentsMargins(10, 10, 10, 10);
        bl->setSpacing(6);
        auto *head = new QHBoxLayout();
        auto *name = new QLabel(QString("第 %1 组").arg(i + 1));
        name->setStyleSheet("color:#c6daf7; font-size:13px; font-weight:800; background:transparent;");
        head->addWidget(name);
        head->addStretch();
        auto *count = new QLabel(QString("%1 台").arg(b.codes.size()));
        count->setStyleSheet("color:#9eb7de; font-size:12px; background:transparent;");
        head->addWidget(count);
        bl->addLayout(head);
        for (const auto &code : b.codes) {
            auto *chipRow = new QHBoxLayout();
            auto *chip = new QPushButton(code);
            chip->setCursor(Qt::PointingHandCursor);
            chip->setStyleSheet(QString(R"(
                QPushButton {
                    border: 1px solid %1; border-radius: 10px;
                    background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445);
                    color: #eaf3ff; font-size: 12px; font-weight: 800; padding: 8px 10px;
                }
                QPushButton:hover { border-color: #56baff; }
            )").arg(m_selectedCode == code ? "#ffcf66" : "#3065ab"));
            connect(chip, &QPushButton::clicked, this, [this, code]() {
                m_selectedCode = (m_selectedCode == code) ? QString() : code;
                render();
            });
            chipRow->addWidget(chip);
            auto *rmBtn = new QPushButton("×");
            rmBtn->setFixedSize(22, 22);
            rmBtn->setCursor(Qt::PointingHandCursor);
            rmBtn->setStyleSheet("QPushButton { border: 1px solid #7a2b2b; border-radius: 8px; background: #3a1414cc; color: #ffb4b4; font-size: 14px; } QPushButton:hover { border-color: #ff6b6b; }");
            QString bId = b.id;
            connect(rmBtn, &QPushButton::clicked, this, [this, code, bId]() {
                for (auto &bk : m_buckets) {
                    if (bk.id == bId) { bk.codes.removeAll(code); break; }
                }
                if (!m_unassigned.contains(code)) m_unassigned.append(code);
                if (m_selectedCode == code) m_selectedCode.clear();
                render();
            });
            chipRow->addWidget(rmBtn);
            bl->addLayout(chipRow);
        }
        // 点击桶添加到该组
        QString bId = b.id;
        // bucket click to add selected device
        bl->addStretch();
        bgl->addWidget(bucket, i / 3, i % 3);
    }

    m_toolbarMeta->setText(QString("分组 %1 / 8 · 未分配 %2 台").arg(m_buckets.size()).arg(m_unassigned.size()));
}

// ═══════════════════════════════════════════════
// 班级同训 实现
// ═══════════════════════════════════════════════
ClassTrainPage::ClassTrainPage(MainWindow *mw, QWidget *parent)
    : PageBase("班级同训", "", mw, parent), m_isGrouped(false), m_isMeasuring(false), m_backTarget("multi"),
      m_studentScroll(nullptr)
{
    connect(m_backBtn, &QPushButton::clicked, mw, [this, mw]() {
        stopMeasuring();
        m_startBtn->setText("开始训练");
        if (m_backTarget == "group") mw->switchPage(11);
        else mw->switchPage(10);
    });
    connect(m_homeBtn, &QPushButton::clicked, mw, [this, mw]() { stopMeasuring(); mw->goHome(); });
    m_backBtn->show();

    auto *body = new QVBoxLayout();
    body->setContentsMargins(24, 16, 24, 16);
    body->setSpacing(12);

    m_subtitleLabel_page = new QLabel();
    m_subtitleLabel_page->hide();

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(0);

    m_startBtn = new QPushButton("开始训练");
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setMinimumWidth(520);
    m_startBtn->setFixedHeight(92);
    m_startBtn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_startBtn->setStyleSheet(R"(
        QPushButton {
            border: 1px solid #2f6ab4; border-radius: 18px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #1d5aa1, stop:1 #123a6f);
            color: #eaf3ff; font-size: 28px; font-weight: 900;
        }
        QPushButton:hover { border-color: #56baff; }
    )");
    connect(m_startBtn, &QPushButton::clicked, this, [this, mw]() {
        if (m_isMeasuring) {
            stopMeasuring();
            m_startBtn->setText("开始训练");
            mw->switchPage(13); // go to summary
        } else {
            startMeasuring();
            m_startBtn->setText("停止训练");
        }
    });
    toolbar->addWidget(m_startBtn, 1);

    body->addLayout(toolbar);

    m_studentScroll = new QScrollArea();
    m_studentScroll->setWidgetResizable(true);
    m_studentScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_studentScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_studentScroll->setStyleSheet(
        "QScrollArea { border: 1px solid #234f8c; border-radius: 16px; background: #07172f99; }");
    m_studentScroll->setMinimumHeight(200);
    m_studentScroll->viewport()->setAttribute(Qt::WA_AcceptTouchEvents, true);
    // 触摸 / 鼠标拖动滚动（linuxfb 上常无独立滚轮）
    QScroller::grabGesture(m_studentScroll->viewport(), QScroller::TouchGesture);
    QScroller::grabGesture(m_studentScroll->viewport(), QScroller::LeftMouseButtonGesture);
    if (QScroller *sc = QScroller::scroller(m_studentScroll->viewport())) {
        QScrollerProperties sp = sc->scrollerProperties();
        sp.setScrollMetric(QScrollerProperties::DragStartDistance, QVariant::fromValue<qreal>(12.0));
        sc->setScrollerProperties(sp);
    }
    m_scrollContent = new QWidget();
    m_scrollContent->setStyleSheet("background:transparent;");
    m_scrollContent->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_scrollContent->setLayout(new QVBoxLayout());
    ((QVBoxLayout *)m_scrollContent->layout())->setSpacing(10);
    ((QVBoxLayout *)m_scrollContent->layout())->setContentsMargins(12, 12, 12, 12);
    m_studentScroll->setWidget(m_scrollContent);
    body->addWidget(m_studentScroll, 1);

    // 必须占满标题栏以下区域，否则 QScrollArea 只有「最小高度」几乎不可滚动
    m_rootLayout->addLayout(body, 1);
}

void ClassTrainPage::initFromNoGroup(const QStringList &deviceCodes, const QList<SleSeekDevice> &seekDevices) {
    stopMeasuring();
    m_backTarget = "multi";
    m_isGrouped = false;
    m_students.clear();
    m_eventsByDevice.clear();

    QMap<QString, QString> codeToMac;
    for (const SleSeekDevice &d : seekDevices) {
        const QString code = d.deviceId > 0 ? QString::number(d.deviceId) : d.mac;
        codeToMac.insert(code, d.mac);
    }

    for (int i = 0; i < deviceCodes.size(); ++i) {
        Student st;
        st.deviceCode = deviceCodes[i];
        st.mac = codeToMac.value(deviceCodes[i]);
        st.displayName = QString("学员 %1").arg(i + 1);
        st.groupTitle = QString();
        st.swings = 0;
        m_students.append(st);
        m_eventsByDevice[deviceCodes[i]] = {};
    }
    m_subtitleLabel_page->clear();
    m_startBtn->setText("开始训练");
    renderUI();
}

void ClassTrainPage::initFromGrouped(const GroupPage *groupPage) {
    stopMeasuring();
    m_backTarget = "group";
    m_isGrouped = true;
    m_students.clear();
    m_eventsByDevice.clear();
    for (int gi = 0; gi < groupPage->m_buckets.size(); ++gi) {
        const auto &b = groupPage->m_buckets[gi];
        for (int j = 0; j < b.codes.size(); ++j) {
            Student st;
            st.deviceCode = b.codes[j];
            st.mac = QString();
            st.displayName = QString("学员 %1").arg(j + 1);
            st.groupTitle = QString("第 %1 组").arg(gi + 1);
            st.swings = 0;
            m_students.append(st);
            m_eventsByDevice[b.codes[j]] = {};
        }
    }
    for (int k = 0; k < groupPage->m_unassigned.size(); ++k) {
        const auto &code = groupPage->m_unassigned[k];
        Student st;
        st.deviceCode = code;
        st.mac = QString();
        st.displayName = QString("未分组 %1").arg(k + 1);
        st.groupTitle = QStringLiteral("未分组");
        st.swings = 0;
        m_students.append(st);
        m_eventsByDevice[code] = {};
    }
    m_subtitleLabel_page->clear();
    m_startBtn->setText("开始训练");
    renderUI();
}

static void classTrainDebugLog(const QString &line)
{
    QFile f(QStringLiteral("/tmp/widget_class_train.log"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"))
       << QLatin1Char(' ') << line << QLatin1Char('\n');
}

static QString normalizeClassMac(const QString &mac)
{
    QString s = mac.trimmed().toUpper();
    s.replace(QLatin1Char('-'), QLatin1Char(':'));
    return s;
}

QString ClassTrainPage::deviceCodeForMac(const QString &mac) const
{
    const QString norm = normalizeClassMac(mac);
    if (norm.isEmpty())
        return QString();

    for (const Student &st : m_students) {
        if (normalizeClassMac(st.mac) == norm)
            return st.deviceCode;
    }
    return QString();
}

void ClassTrainPage::startMeasuring() {
    m_isMeasuring = true;
    m_imuSubscribed = true;
    if (m_startBtn)
        m_startBtn->setText(QStringLiteral("停止训练"));

    if (m_mainWindow && m_mainWindow->sleImu()) {
        m_mainWindow->sleImu()->resetSwingDetector();
        m_mainWindow->sleImu()->start();
    }
    classTrainDebugLog(QStringLiteral("startMeasuring students=%1").arg(m_students.size()));
}

void ClassTrainPage::stopMeasuring() {
    m_isMeasuring = false;
    m_imuSubscribed = false;
    if (m_startBtn)
        m_startBtn->setText(QStringLiteral("开始训练"));
    classTrainDebugLog(QStringLiteral("stopMeasuring"));
}

void ClassTrainPage::recordClassHit(const QString &deviceCode, int speedKmh, int powerTen, int score,
    const QString &hitType, int durationMs)
{
    if (!m_isMeasuring || deviceCode.isEmpty())
        return;

    const QString typeLabel = hitType.trimmed().isEmpty() ? QStringLiteral("挥拍") : hitType.trimmed();

    for (auto &st : m_students) {
        if (st.deviceCode != deviceCode)
            continue;
        st.swings++;
        QMap<QString, QVariant> ev;
        ev[QStringLiteral("idx")] = st.swings;
        ev[QStringLiteral("score")] = score;
        ev[QStringLiteral("hitType")] = typeLabel;
        ev[QStringLiteral("speedKmh")] = speedKmh;
        ev[QStringLiteral("power")] = powerTen;
        if (durationMs >= 0)
            ev[QStringLiteral("durationMs")] = durationMs;
        ev[QStringLiteral("efficiency")] = qBound(30, score, 100);
        m_eventsByDevice[st.deviceCode].append(ev);
        renderUI();
        return;
    }
}

void ClassTrainPage::onImuHitDetected(const QString &mac, double speedKmh, int powerTen, double, double, int durationMs,
    const QString &hitType, int strokeClassId, float strokeConfidence)
{
    if (!m_imuSubscribed || !m_isMeasuring)
        return;

    const QString deviceCode = deviceCodeForMac(mac);
    if (deviceCode.isEmpty()) {
        classTrainDebugLog(QStringLiteral("onImuHit ignored mac=%1").arg(mac));
        return;
    }

    const int speed = displaySpeedKmh(speedKmh);
    const int power = qBound(1, powerTen, 10);
    const int score = classHitScoreFromImu(strokeClassId, hitType, strokeConfidence, power);
    recordClassHit(deviceCode, speed, power, score, hitType, durationMs);
    classTrainDebugLog(QStringLiteral("onImuHit mac=%1 code=%2 type=%3 cls=%4 conf=%5 speed=%6 power=%7 score=%8 dur=%9ms")
                           .arg(mac, deviceCode, hitType)
                           .arg(strokeClassId)
                           .arg(strokeConfidence, 0, 'f', 3)
                           .arg(speed)
                           .arg(power)
                           .arg(score)
                           .arg(durationMs));
}

void ClassTrainPage::appendTrainingSession(const QString &deviceCode, const QList<int> &scores,
                                           const QList<int> &speedsKmh, const QList<int> &powersTen,
                                           const QList<QString> &hitTypes)
{
    QList<QMap<QString, QVariant>> events;
    for (int i = 0; i < scores.size(); ++i) {
        QMap<QString, QVariant> ev;
        ev[QStringLiteral("idx")] = i + 1;
        ev[QStringLiteral("score")] = scores[i];
        const QString type = (i < hitTypes.size() && !hitTypes[i].trimmed().isEmpty())
                                 ? hitTypes[i].trimmed()
                                 : QStringLiteral("挥拍");
        ev[QStringLiteral("hitType")] = type;
        if (i < speedsKmh.size())
            ev[QStringLiteral("speedKmh")] = speedsKmh[i];
        if (i < powersTen.size())
            ev[QStringLiteral("power")] = powersTen[i];
        ev[QStringLiteral("efficiency")] = qBound(30, scores[i], 100);
        events.append(ev);
    }
    m_eventsByDevice[deviceCode] = events;
    for (auto &st : m_students) {
        if (st.deviceCode == deviceCode) {
            st.swings = events.size();
            break;
        }
    }
    renderUI();
}

int getClassStudentAvgScore(const QList<QMap<QString, QVariant>> &events) {
    if (events.isEmpty()) return 0;
    int sum = 0;
    for (const auto &e : events) sum += e["score"].toInt();
    return sum / events.size();
}

// 学员卡片：勿在 QPushButton 内再套 QLabel + layout，否则子控件抢点击且部分环境下高度计算异常导致重叠。
static constexpr int kClassStudentCardW = 300;
static constexpr int kClassStudentCardH = 228;
static constexpr int kClassStudentGridCols = 2;

static QPushButton *makeClassStudentCard(const QString &displayName, const QString &deviceCode, int avg,
                                         int swings, const QString &lastHitType = QString())
{
    auto *card = new QPushButton();
    card->setFixedSize(kClassStudentCardW, kClassStudentCardH);
    card->setCursor(Qt::PointingHandCursor);
    const QString devShort =
        deviceCode.length() > 14 ? (deviceCode.left(13) + QString::fromUtf8("…")) : deviceCode;
    const QString typeLine = lastHitType.trimmed().isEmpty()
                                 ? QString::fromUtf8("最近: —")
                                 : QString::fromUtf8("最近: %1").arg(lastHitType.trimmed());
    card->setText(QString::fromUtf8("%1\n%2\n平均 %3 分\n挥拍 %4 次\n%5")
                      .arg(displayName)
                      .arg(devShort)
                      .arg(avg > 0 ? avg : 0)
                      .arg(swings)
                      .arg(typeLine));
    card->setStyleSheet(QString::fromUtf8(R"(
        QPushButton {
            border: 1px solid #2e63ac; border-radius: 16px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445);
            color: #eaf3ff;
            font-size: 20px;
            font-weight: 800;
            padding: 14px 12px;
            text-align: center;
        }
        QPushButton:hover { border-color: #56baff; }
    )"));
    return card;
}

static void applyClassStudentGridStyling(QGridLayout *gl)
{
    gl->setContentsMargins(0, 0, 0, 0);
    gl->setSpacing(16);
    for (int c = 0; c < kClassStudentGridCols; ++c)
        gl->setColumnStretch(c, 1);
}

static void finalizeClassStudentGrid(QGridLayout *gl, int nCards)
{
    if (nCards <= 0 || !gl)
        return;
    const int rows = (nCards + kClassStudentGridCols - 1) / kClassStudentGridCols;
    const int vsp = gl->verticalSpacing() > 0 ? gl->verticalSpacing() : 14;
    const int rowH = kClassStudentCardH + 8;
    for (int r = 0; r < rows; ++r)
        gl->setRowMinimumHeight(r, rowH);
    if (auto *gw = qobject_cast<QWidget *>(gl->parent()))
        gw->setMinimumHeight(rows * rowH + qMax(0, rows - 1) * vsp);
}

void ClassTrainPage::renderUI() {
    QVBoxLayout *layout = (QVBoxLayout *)m_scrollContent->layout();
    QLayoutItem *child;
    while ((child = layout->takeAt(0)) != nullptr) {
        if (child->widget())
            delete child->widget();
        delete child;
    }

    if (m_isGrouped) {
        QMap<QString, QList<Student *>> groups;
        QList<Student *> ungrouped;
        for (auto &st : m_students) {
            if (st.groupTitle.isEmpty() || st.groupTitle == "未分组")
                ungrouped.append(&st);
            else
                groups[st.groupTitle].append(&st);
        }
        for (auto it = groups.begin(); it != groups.end(); ++it) {
            auto *sectionTitle = new QLabel(it.key());
            sectionTitle->setStyleSheet(
                "color:#8cc7ff; font-size:20px; font-weight:900; background:transparent; margin-top:10px;");
            layout->addWidget(sectionTitle);
            auto *grid = new QWidget();
            auto *gl = new QGridLayout(grid);
            applyClassStudentGridStyling(gl);
            int col = 0;
            for (auto *st : it.value()) {
                const int avg = getClassStudentAvgScore(m_eventsByDevice[st->deviceCode]);
                QString lastType;
                const auto evs = m_eventsByDevice.value(st->deviceCode);
                if (!evs.isEmpty()) {
                    lastType = evs.last().value(QStringLiteral("hitType")).toString();
                }
                auto *card = makeClassStudentCard(st->displayName, st->deviceCode, avg, st->swings, lastType);
                const QString dc = st->deviceCode;
                connect(card, &QPushButton::clicked, this, [this, dc]() { emit studentClicked(dc); });
                gl->addWidget(card, col / kClassStudentGridCols, col % kClassStudentGridCols);
                col++;
            }
            finalizeClassStudentGrid(gl, col);
            layout->addWidget(grid);
        }
        if (!ungrouped.isEmpty()) {
            auto *sectionTitle = new QLabel("未分组");
            sectionTitle->setStyleSheet(
                "color:#8cc7ff; font-size:20px; font-weight:900; background:transparent; margin-top:10px;");
            layout->addWidget(sectionTitle);
            auto *grid = new QWidget();
            auto *gl = new QGridLayout(grid);
            applyClassStudentGridStyling(gl);
            int col = 0;
            for (auto *st : ungrouped) {
                const int avg = getClassStudentAvgScore(m_eventsByDevice[st->deviceCode]);
                QString lastType;
                const auto evs = m_eventsByDevice.value(st->deviceCode);
                if (!evs.isEmpty()) {
                    lastType = evs.last().value(QStringLiteral("hitType")).toString();
                }
                auto *card = makeClassStudentCard(st->displayName, st->deviceCode, avg, st->swings, lastType);
                const QString dc = st->deviceCode;
                connect(card, &QPushButton::clicked, this, [this, dc]() { emit studentClicked(dc); });
                gl->addWidget(card, col / kClassStudentGridCols, col % kClassStudentGridCols);
                col++;
            }
            finalizeClassStudentGrid(gl, col);
            layout->addWidget(grid);
        }
    } else {
        auto *grid = new QWidget();
        auto *gl = new QGridLayout(grid);
        applyClassStudentGridStyling(gl);
        int col = 0;
        for (auto &st : m_students) {
            const int avg = getClassStudentAvgScore(m_eventsByDevice[st.deviceCode]);
            QString lastType;
            const auto evs = m_eventsByDevice.value(st.deviceCode);
            if (!evs.isEmpty()) {
                lastType = evs.last().value(QStringLiteral("hitType")).toString();
            }
            auto *card = makeClassStudentCard(st.displayName, st.deviceCode, avg, st.swings, lastType);
            const QString dc = st.deviceCode;
            connect(card, &QPushButton::clicked, this, [this, dc]() { emit studentClicked(dc); });
            gl->addWidget(card, col / kClassStudentGridCols, col % kClassStudentGridCols);
            col++;
        }
        finalizeClassStudentGrid(gl, col);
        layout->addWidget(grid);
    }

    syncStudentScrollContentSize();
}

void ClassTrainPage::syncStudentScrollContentSize()
{
    if (!m_studentScroll || !m_scrollContent)
        return;
    QLayout *lay = m_scrollContent->layout();
    if (!lay)
        return;
    lay->invalidate();
    lay->activate();
    const int vw = qMax(1, m_studentScroll->viewport()->width());
    const QSize minLay = lay->minimumSize();
    const QSize hintLay = lay->sizeHint();
    // 不用 heightForWidth：嵌套 QGridLayout 在部分环境下会低估高度，配合 setMinimumHeight 会把学员格压成堆叠
    const int needH = qMax(minLay.height(), hintLay.height());
    m_scrollContent->setMinimumWidth(vw);
    m_scrollContent->setMinimumHeight(qMax(needH, 1));
    m_scrollContent->updateGeometry();
    m_studentScroll->updateGeometry();
}

void ClassTrainPage::showEvent(QShowEvent *e)
{
    PageBase::showEvent(e);
    syncStudentScrollContentSize();
}

void ClassTrainPage::resizeEvent(QResizeEvent *e)
{
    PageBase::resizeEvent(e);
    syncStudentScrollContentSize();
}

// ═══════════════════════════════════════════════
// 班级训练总结 实现
// ═══════════════════════════════════════════════
ClassTrainSummaryPage::ClassTrainSummaryPage(MainWindow *mw, QWidget *parent)
    : PageBase("班级训练总结", "", mw, parent)
{
    connect(m_backBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(12); });
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();
    m_backBtn->setText("返回班级同训");

    auto *body = new QVBoxLayout();
    body->setContentsMargins(24, 16, 24, 16);
    body->setSpacing(12);

    auto *sub = new QLabel("训练结束，已生成学员排名与建议（占位）。");
    sub->setStyleSheet("font-size:13px; color:#9eb7de; background:transparent;");
    sub->setAlignment(Qt::AlignCenter);
    body->addWidget(sub);

    auto *topRow = new QHBoxLayout();
    topRow->setSpacing(14);
    m_studentCount = new QLabel("学员总数：0");
    m_studentCount->setStyleSheet("font-size:20px; font-weight:900; color:#8cc7ff; background:transparent;");
    topRow->addWidget(m_studentCount);
    m_classAvg = new QLabel("全班平均分：--");
    m_classAvg->setStyleSheet("font-size:20px; font-weight:900; color:#8cc7ff; background:transparent;");
    topRow->addWidget(m_classAvg);
    m_totalSwings = new QLabel("总挥拍次数：0");
    m_totalSwings->setStyleSheet("font-size:20px; font-weight:900; color:#8cc7ff; background:transparent;");
    topRow->addWidget(m_totalSwings);
    topRow->addStretch();
    body->addLayout(topRow);

    auto *scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: 1px solid #234f8c; border-radius: 16px; background: #07172f99; }");
    m_listWidget = new QWidget();
    m_listWidget->setStyleSheet("background:transparent;");
    m_listWidget->setLayout(new QVBoxLayout());
    ((QVBoxLayout*)m_listWidget->layout())->setSpacing(12);
    ((QVBoxLayout*)m_listWidget->layout())->setContentsMargins(12, 12, 12, 12);
    scrollArea->setWidget(m_listWidget);
    body->addWidget(scrollArea, 1);

    m_rootLayout->addLayout(body);
}

void ClassTrainSummaryPage::showSummary(ClassTrainPage *ct) {
    QVBoxLayout *layout = (QVBoxLayout*)m_listWidget->layout();
    QLayoutItem *child;
    while ((child = layout->takeAt(0)) != nullptr) {
        if (child->widget()) delete child->widget();
        delete child;
    }

    struct Row { QString deviceCode; QString displayName; QString groupTitle; int swings; int avg; QString suggestion; };
    QList<Row> rows;
    for (const auto &st : ct->m_students) {
        const auto &events = ct->m_eventsByDevice[st.deviceCode];
        int swings = events.size();
        int avg = swings > 0 ? std::accumulate(events.begin(), events.end(), 0,
            [](int acc, const QMap<QString,QVariant> &e) { return acc + e["score"].toInt(); }) / swings : 0;
        QString suggestion;
        if (swings < 6) suggestion = "建议增加练习次数，先保证稳定击球。";
        else if (avg >= 88) suggestion = "表现稳定，可提高训练强度并挑战更快节奏。";
        else if (avg >= 75) suggestion = "整体良好，建议重点优化发力连贯性。";
        else if (avg >= 62) suggestion = "建议优先提升击球稳定性与动作一致性。";
        else suggestion = "建议降低节奏，先把动作做标准再提速。";
        rows.append({st.deviceCode, st.displayName, st.groupTitle, swings, avg, suggestion});
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        return a.avg > b.avg || (a.avg == b.avg && a.swings > b.swings);
    });

    int totalStudents = rows.size();
    int totalSwings = 0;
    int sumAvg = 0;
    for (const auto &r : rows) { totalSwings += r.swings; sumAvg += r.avg; }
    int classAvg = totalStudents > 0 ? sumAvg / totalStudents : 0;

    m_studentCount->setText(QString("学员总数：%1").arg(totalStudents));
    m_classAvg->setText(QString("全班平均分：%1 分").arg(classAvg));
    m_totalSwings->setText(QString("总挥拍次数：%1").arg(totalSwings));

    for (int i = 0; i < rows.size(); ++i) {
        const auto &r = rows[i];
        auto *row = new QPushButton();
        row->setCursor(Qt::PointingHandCursor);
        row->setMinimumHeight(132);
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        row->setStyleSheet(R"(
            QPushButton {
                border: 1px solid #2e63ac; border-radius: 18px;
                background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445);
                color: #eaf3ff; text-align: left; padding: 16px 20px;
            }
            QPushButton:hover { border-color: #56baff; }
        )");
        auto *rl = new QHBoxLayout(row);
        rl->setSpacing(18);
        rl->setContentsMargins(6, 6, 6, 6);
        auto *rank = new QLabel(QString("#%1").arg(i + 1));
        rank->setFixedWidth(64);
        rank->setStyleSheet("font-size:32px; font-weight:900; color:#ffcf66; background:transparent;");
        rank->setAlignment(Qt::AlignCenter);
        rl->addWidget(rank);
        auto *info = new QVBoxLayout();
        info->setSpacing(8);
        auto *nameLabel = new QLabel(r.displayName + (r.groupTitle.isEmpty() ? "" : " · " + r.groupTitle));
        nameLabel->setStyleSheet("font-size:22px; font-weight:800; background:transparent; color:#eaf3ff;");
        info->addWidget(nameLabel);
        auto *sugLabel = new QLabel(r.suggestion);
        sugLabel->setStyleSheet("font-size:18px; color:#d9e9ff; background:transparent;");
        sugLabel->setWordWrap(true);
        info->addWidget(sugLabel);
        rl->addLayout(info, 1);
        auto *kpis = new QHBoxLayout();
        kpis->setSpacing(14);
        auto *avgKpi = new QLabel(QString("平均分\n%1").arg(r.avg));
        avgKpi->setMinimumWidth(108);
        avgKpi->setStyleSheet("font-size:17px; font-weight:800; color:#cfe2ff; background:transparent; border:1px solid #2e63ac; border-radius:14px; padding:12px 14px;");
        avgKpi->setAlignment(Qt::AlignCenter);
        kpis->addWidget(avgKpi);
        auto *swingKpi = new QLabel(QString("挥拍次数\n%1").arg(r.swings));
        swingKpi->setMinimumWidth(108);
        swingKpi->setStyleSheet("font-size:17px; font-weight:800; color:#cfe2ff; background:transparent; border:1px solid #2e63ac; border-radius:14px; padding:12px 14px;");
        swingKpi->setAlignment(Qt::AlignCenter);
        kpis->addWidget(swingKpi);
        rl->addLayout(kpis);
        QString dc = r.deviceCode;
        connect(row, &QPushButton::clicked, this, [this, dc]() { emit studentClicked(dc); });
        layout->addWidget(row);
    }
    layout->addStretch();
}
