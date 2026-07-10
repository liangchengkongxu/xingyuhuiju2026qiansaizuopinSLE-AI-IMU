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

// ═══════════════════════════════════════════════
// 技能详情 实现
// ═══════════════════════════════════════════════
static QStringList skillTipsPoints(const QString &skillName)
{
    const QString key = skillName.trimmed();
    if (key == QStringLiteral("杀球"))
        return {QStringLiteral("侧身反弓引拍，蹬地转体发力"),
                QStringLiteral("右肩前上方最高点击球，闪腕下压")};
    if (key == QStringLiteral("挑球"))
        return {QStringLiteral("屈膝低重心，拍面略仰"),
                QStringLiteral("前臂旋转、手腕闪动击托底部")};
    if (key == QStringLiteral("放网"))
        return {QStringLiteral("上网到位，握拍放松"), QStringLiteral("手指手腕轻送，托球过网")};
    if (key == QStringLiteral("高远"))
        return {QStringLiteral("侧身充分引拍，后腿蹬地转体"),
                QStringLiteral("右肩前上方高点击球，闪腕发力")};
    if (key == QStringLiteral("平抽"))
        return {QStringLiteral("半蹲举拍，身前高位击球"),
                QStringLiteral("手腕手指寸劲，小引拍快挥拍")};
    return {};
}

static QStringList skillTipsWarnings(const QString &skillName)
{
    const QString key = skillName.trimmed();
    if (key == QStringLiteral("杀球"))
        return {QStringLiteral("忌甩大臂蛮力，易伤肩且球慢"), QStringLiteral("杀完迅速回位")};
    if (key == QStringLiteral("挑球"))
        return {QStringLiteral("击球点在侧前方，忌过低"), QStringLiteral("挑完快回场地中心")};
    if (key == QStringLiteral("放网"))
        return {QStringLiteral("拍面平贴网、斜放远网"), QStringLiteral("借力即可，忌用力过大")};
    if (key == QStringLiteral("高远"))
        return {QStringLiteral("力量要“弹”出，忌手臂硬推"), QStringLiteral("击球后重心前移并回位")};
    if (key == QStringLiteral("平抽"))
        return {QStringLiteral("打完立即举拍预备下一拍"), QStringLiteral("配合步法，忌只动上身找球")};
    return {};
}

static QLabel *makeSkillTipsHeading(const QString &text, const char *color)
{
    auto *label = new QLabel(text);
    label->setStyleSheet(
        QStringLiteral("font-size:34px; font-weight:800; color:%1; background:transparent;").arg(
            QString::fromUtf8(color)));
    return label;
}

static QLabel *makeSkillTipsBullet(const QString &text)
{
    auto *label = new QLabel(QStringLiteral("· ") + text);
    label->setWordWrap(true);
    label->setStyleSheet(
        QStringLiteral("font-size:30px; font-weight:700; color:#eef4ff; background:transparent;"));
    return label;
}

SkillDetailPage::SkillDetailPage(MainWindow *mw, QWidget *parent)
    : PageBase("基础动作", "", mw, parent, PageHeaderMode::SingleCentered)
{
    connect(m_backBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(2); });
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();
    m_backBtn->setObjectName(QString());
    m_homeBtn->setObjectName(QString());
    m_backBtn->setStyleSheet(QString(R"(
        QPushButton {
            font-size: 18px; font-weight: 800; color: #062a14;
            border: 2px solid %1; border-radius: 999px;
            padding: 14px 26px; min-height: 52px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #45e08a, stop:1 %1);
        }
        QPushButton:hover { border-color: #7ef0b0; background: #5ef09a; }
    )").arg(COLOR_OK));
    m_homeBtn->setStyleSheet(QString(R"(
        QPushButton {
            font-size: 18px; font-weight: 800; color: #fff5f5;
            border: 2px solid %1; border-radius: 999px;
            padding: 14px 26px; min-height: 52px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #ff6b6b, stop:1 %1);
        }
        QPushButton:hover { border-color: #ff9a9a; background: #ff5252; }
    )").arg(COLOR_BAD));

    auto *body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(16);

    /* 左侧：原边栏位置放动作要领占位 */
    auto *leftCol = new QWidget();
    leftCol->setFixedWidth(kSkillDetailLeftColW);
    leftCol->setStyleSheet("background:transparent;");
    auto *leftLay = new QVBoxLayout(leftCol);
    leftLay->setContentsMargins(12, 8, 4, 12);
    leftLay->setSpacing(0);

    auto *tipsBox = new QFrame();
    tipsBox->setObjectName("skillTipsBox");
    tipsBox->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    tipsBox->setStyleSheet(
        "QFrame#skillTipsBox {"
        "  border: 1px solid #234f8c;"
        "  border-radius: 16px;"
        "  background: rgba(8,26,51,184);"
        "}");
    auto *tbLay = new QVBoxLayout(tipsBox);
    tbLay->setContentsMargins(20, 18, 20, 18);
    tbLay->setSpacing(0);
    auto *tt = new QLabel(QStringLiteral("动作要领"));
    tt->setStyleSheet("font-size:36px; font-weight:800; color:#8cc7ff; background:transparent;");
    tbLay->addWidget(tt);
    tbLay->addSpacing(6);

    auto *tipsContentHost = new QWidget();
    tipsContentHost->setStyleSheet("background:transparent;");
    m_tipsContentLay = new QVBoxLayout(tipsContentHost);
    m_tipsContentLay->setContentsMargins(0, 0, 0, 0);
    m_tipsContentLay->setSpacing(0);
    tbLay->addWidget(tipsContentHost, 1);
    leftLay->addWidget(tipsBox, 1);
    body->addWidget(leftCol);

    auto *content = new QWidget();
    content->setStyleSheet("background:transparent;");
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(4, 6, 16, 24);
    cl->setSpacing(0);

    const int videoInnerW = kSkillDetailVideoW;
    const int videoInnerH = kSkillDetailVideoH;
    const int frameW = videoInnerW + kSkillDetailVideoPad * 2;
    const int frameH = videoInnerH + kSkillDetailVideoPad * 2;

    auto *videoFrame = new QFrame();
    videoFrame->setObjectName("skillVideoFrame");
    videoFrame->setFixedSize(frameW, frameH);
    videoFrame->setStyleSheet(
        "QFrame#skillVideoFrame {"
        "  border: 1px solid #2e63ac;"
        "  border-radius: 18px;"
        "  background: #000000;"
        "}");
    auto *vfLay = new QVBoxLayout(videoFrame);
    vfLay->setContentsMargins(kSkillDetailVideoPad, kSkillDetailVideoPad, kSkillDetailVideoPad, kSkillDetailVideoPad);
    vfLay->setSpacing(0);

    auto *videoHost = new QWidget(videoFrame);
    videoHost->setFixedSize(videoInnerW, videoInnerH);
    videoHost->setStyleSheet(QStringLiteral("background:#000;"));

    m_videoStack = new QStackedWidget(videoHost);
    m_videoStack->setGeometry(0, 0, videoInnerW, videoInnerH);
    m_tutorialVideo = new QVideoWidget();
    m_tutorialVideo->setMinimumSize(videoInnerW, videoInnerH);
    m_tutorialVideo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_tutorialVideo->setAspectRatioMode(Qt::KeepAspectRatio);
    m_tutorialVideo->setStyleSheet("background-color: #000000;");
    m_tutorialPlaceholder = new QLabel(QStringLiteral(
        "暂无教学视频\n\n请将「仅画面、无音轨」的 H.264 MP4 放到：\n/opt/widget_ui/tutorials/<技能名>.mp4\n\n"
        "（设备无扬声器；带 AAC 音轨时板端常缺 GStreamer 音频解码插件，仍会报错。）\n\n"
        "PC 上去音轨示例：\nffmpeg -y -i 原片.mp4 -c:v copy -an 高远.mp4"));
    m_tutorialPlaceholder->setAlignment(Qt::AlignCenter);
    m_tutorialPlaceholder->setWordWrap(true);
    m_tutorialPlaceholder->setStyleSheet(
        "color:#b8d5ff; font-size:20px; letter-spacing:0.5px; background:transparent;");
    auto *videoPage = new QWidget();
    m_tutorialVideoPage = videoPage;
    auto *videoPageLay = new QVBoxLayout(videoPage);
    videoPageLay->setContentsMargins(0, 0, 0, 0);
    videoPageLay->addWidget(m_tutorialVideo);
    m_videoStack->addWidget(videoPage);
    m_videoStack->addWidget(m_tutorialPlaceholder);
    m_videoStack->setCurrentWidget(m_tutorialPlaceholder);

    m_tutorialPlayer = new QMediaPlayer(this, QMediaPlayer::VideoSurface);
    m_tutorialPlaylist = new QMediaPlaylist(this);
    m_tutorialPlayer->setPlaylist(m_tutorialPlaylist);
    m_tutorialPlayer->setVideoOutput(m_tutorialVideo);
    m_tutorialPlayer->setMuted(true);

    connect(m_tutorialPlayer, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, [this](QMediaPlayer::Error) {
        if (m_tutorialPlayer == nullptr)
            return;
        qWarning() << "[SkillDetail] QMediaPlayer:" << m_tutorialPlayer->errorString();
        m_videoStack->setCurrentWidget(m_tutorialPlaceholder);
        m_tutorialPlaceholder->setText(
            QStringLiteral("无法播放该视频\n%1\n\n本机无扬声器，建议使用无音轨 MP4（避免装 AAC 解码插件）。\n"
                           "PC 示例：ffmpeg -y -i 原片.mp4 -c:v copy -an 高远.mp4")
                .arg(m_tutorialPlayer->errorString()));
    });

    vfLay->addWidget(videoHost, 0, Qt::AlignCenter);

    constexpr int kTutorialSpeedOverlayH = 60;
    auto *speedOverlay = new QFrame(videoHost);
    speedOverlay->setGeometry(0, videoInnerH - kTutorialSpeedOverlayH, videoInnerW, kTutorialSpeedOverlayH);
    speedOverlay->setStyleSheet(QStringLiteral("QFrame { background: rgba(0,0,0,175); border: none; }"));
    auto *speedBtnRow = new QHBoxLayout(speedOverlay);
    speedBtnRow->setContentsMargins(12, 6, 12, 6);
    speedBtnRow->setSpacing(12);
    speedBtnRow->addStretch(1);

    struct TutorialRateBtn {
        double r;
        const char *label;
    };
    const TutorialRateBtn tutorialRates[] = {{0.5, "0.5x"}, {0.75, "0.75x"}, {1.0, "1.0x"}, {1.5, "1.5x"}};
    m_tutorialSpeedButtons.clear();
    QPushButton *defaultTutorialSpeed = nullptr;
    for (const auto &rb : tutorialRates) {
        auto *b = new QPushButton(QString::fromUtf8(rb.label), speedOverlay);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedHeight(48);
        b->setStyleSheet(QLatin1String(VIDEO_SPEED_BTN_NORMAL));
        m_tutorialSpeedButtons.append(b);
        speedBtnRow->addWidget(b);
        if (qFuzzyCompare(rb.r, 1.0))
            defaultTutorialSpeed = b;
        connect(b, &QPushButton::clicked, this, [this, b, rate = rb.r]() {
            highlightTutorialSpeedButton(b, rate);
        });
    }
    speedBtnRow->addStretch(1);
    speedOverlay->raise();

    auto *videoRow = new QHBoxLayout();
    videoRow->setContentsMargins(0, 0, 0, 0);
    videoRow->addStretch(1);
    videoRow->addWidget(videoFrame);
    videoRow->addStretch(1);
    cl->addLayout(videoRow, 1);

    if (defaultTutorialSpeed)
        highlightTutorialSpeedButton(defaultTutorialSpeed, 1.0);

    auto *startBtn = new QPushButton(QStringLiteral("开始练习"));
    startBtn->setMinimumWidth(kSkillDetailStartBtnW);
    startBtn->setMaximumWidth(kSkillDetailStartBtnW);
    startBtn->setFixedHeight(132);
    startBtn->setCursor(Qt::PointingHandCursor);
    startBtn->setStyleSheet(QString(R"(
        QPushButton {
            border: 2px solid %1;
            border-radius: 22px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #45e08a, stop:1 %2);
            color: #062a14;
            font-size: 36px;
            font-weight: 800;
            padding: 18px 40px;
        }
        QPushButton:hover {
            border-color: #7ef0b0;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #5ef09a, stop:1 #38d07e);
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #2fb86a, stop:1 #1a8f52);
        }
    )").arg(COLOR_OK, COLOR_OK));
    connect(startBtn, &QPushButton::clicked, this, [this, mw]() {
        emit startPractice();
        mw->switchPage(4);
    });
    auto *btnRow = new QHBoxLayout();
    btnRow->setContentsMargins(0, 40, 0, 0);
    btnRow->addStretch(1);
    btnRow->addWidget(startBtn);
    btnRow->addStretch(1);
    cl->addLayout(btnRow, 0);

    body->addWidget(content, 1);
    m_rootLayout->addLayout(body);

    refreshTipsContent(QString());
}

void SkillDetailPage::highlightTutorialSpeedButton(QPushButton *active, double rate)
{
    m_tutorialPlaybackRate = rate;
    for (auto *b : m_tutorialSpeedButtons)
        b->setStyleSheet(QLatin1String(VIDEO_SPEED_BTN_NORMAL));
    if (active)
        active->setStyleSheet(QLatin1String(VIDEO_SPEED_BTN_ACTIVE));
    applyTutorialPlaybackRate();
}

void SkillDetailPage::applyTutorialPlaybackRate()
{
    if (m_tutorialPlayer != nullptr)
        m_tutorialPlayer->setPlaybackRate(static_cast<qreal>(m_tutorialPlaybackRate));
}

void SkillDetailPage::refreshTipsContent(const QString &name)
{
    if (!m_tipsContentLay)
        return;

    QLayoutItem *item = nullptr;
    while ((item = m_tipsContentLay->takeAt(0)) != nullptr) {
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    const QStringList points = skillTipsPoints(name);
    const QStringList warns = skillTipsWarnings(name);
    const auto addGap = [this]() { m_tipsContentLay->addStretch(1); };

    if (points.isEmpty()) {
        addGap();
        m_tipsContentLay->addWidget(makeSkillTipsBullet(QStringLiteral("请选择练习动作，查看对应要领。")));
        addGap();
        return;
    }

    addGap();
    m_tipsContentLay->addWidget(makeSkillTipsHeading(QStringLiteral("要点"), COLOR_ACCENT));
    addGap();
    for (const QString &line : points) {
        m_tipsContentLay->addWidget(makeSkillTipsBullet(line));
        addGap();
    }
    m_tipsContentLay->addWidget(makeSkillTipsHeading(QStringLiteral("注意"), COLOR_WARN));
    addGap();
    for (const QString &line : warns) {
        m_tipsContentLay->addWidget(makeSkillTipsBullet(line));
        addGap();
    }
}

static QString resolveTutorialVideoPath(const QString &skillName)
{
    const QString fileName = skillName + QStringLiteral(".mp4");
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/tutorials/") + fileName,
        QStringLiteral("/opt/widget_ui/tutorials/") + fileName,
    };
    for (const QString &path : candidates) {
        const QFileInfo fi(path);
        if (fi.exists() && fi.isFile() && fi.size() > 0)
            return fi.absoluteFilePath();
    }
    return candidates.last();
}

void SkillDetailPage::playTutorialForSkill(const QString &name)
{
    if (m_tutorialPlayer == nullptr || m_videoStack == nullptr)
        return;

    m_tutorialPlayer->stop();
    if (m_tutorialPlaylist != nullptr)
        m_tutorialPlaylist->clear();

    const QString path = resolveTutorialVideoPath(name);
    const QFileInfo fi(path);
    if (fi.exists() && fi.isFile() && fi.size() > 0) {
        m_videoStack->setCurrentWidget(m_tutorialVideoPage);
        m_tutorialPlaylist->addMedia(QMediaContent(QUrl::fromLocalFile(fi.absoluteFilePath())));
        m_tutorialPlaylist->setPlaybackMode(QMediaPlaylist::CurrentItemInLoop);
        m_tutorialPlayer->setPlaybackRate(static_cast<qreal>(m_tutorialPlaybackRate));
        m_tutorialPlayer->play();
    } else {
        m_videoStack->setCurrentWidget(m_tutorialPlaceholder);
        m_tutorialPlaceholder->setText(
            QStringLiteral("暂无「%1」的教学视频\n\n请将 MP4 保存为：\n/opt/widget_ui/tutorials/%2\n\n建议无音轨 H.264（ffmpeg: -c:v copy -an）。")
                .arg(name, name + QStringLiteral(".mp4")));
    }
}

void SkillDetailPage::setSkillName(const QString &name)
{
    m_currentSkillName = name;
    m_titleLabel->setText(name);
    refreshTipsContent(name);
    playTutorialForSkill(name);
}

void SkillDetailPage::showEvent(QShowEvent *event)
{
    if (!m_currentSkillName.isEmpty())
        playTutorialForSkill(m_currentSkillName);
    PageBase::showEvent(event);
}

void SkillDetailPage::hideEvent(QHideEvent *event)
{
    if (m_tutorialPlayer != nullptr)
        m_tutorialPlayer->stop();
    if (m_tutorialPlaylist != nullptr)
        m_tutorialPlaylist->clear();
    PageBase::hideEvent(event);
}
