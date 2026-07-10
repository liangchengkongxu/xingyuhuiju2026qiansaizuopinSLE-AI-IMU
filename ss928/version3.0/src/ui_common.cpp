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

 QSize linuxFbSizeFromPlatformEnv()
{
    const QByteArray plat = qgetenv("QT_QPA_PLATFORM");
    const int idx = plat.indexOf("size=");
    if (idx < 0)
        return QSize(1920, 1080);

    QByteArray tail = plat.mid(idx + 5);
    int len = 0;
    while (len < tail.size()) {
        const char c = tail[len];
        if (c == ':' || c == ',' || c == ' ' || c == '\t')
            break;
        ++len;
    }
    tail.truncate(len);
    const int xpos = tail.indexOf('x');
    if (xpos <= 0)
        return QSize(1920, 1080);

    bool okw = false;
    bool okh = false;
    const int w = tail.left(xpos).toInt(&okw);
    const int h = tail.mid(xpos + 1).toInt(&okh);
    if (!okw || !okh || w < 320 || h < 240)
        return QSize(1920, 1080);
    return QSize(w, h);
}

 int randInt(int lo, int hi) {
    return lo + QRandomGenerator::global()->bounded(hi - lo + 1);
}

/** 挥拍估算球速 → 界面整数 km/h（羽毛球合理区间） */
 int displaySpeedKmh(double speedKmh)
{
    return qBound(8, (int)qRound(speedKmh), 280);
}

/** WIDGET_HIT_SOURCE: camera（默认）| imu | both */
 bool widgetUseCameraHits()
{
    const QByteArray v = qgetenv("WIDGET_HIT_SOURCE").trimmed().toLower();
    if (v.isEmpty() || v == "camera" || v == "cam" || v == "vision" || v == "ai")
        return true;
    if (v == "imu" || v == "racket" || v == "sle")
        return false;
    return true;
}

 bool widgetUseImuHits()
{
    const QByteArray v = qgetenv("WIDGET_HIT_SOURCE").trimmed().toLower();
    return (v == "imu" || v == "racket" || v == "sle" || v == "both");
}

/** 单人练习：多源击球去重间隔（ms）；0=不去重 */
 int practiceHitCooldownMs()
{
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("WIDGET_PRACTICE_HIT_COOLDOWN_MS", &ok);
    if (ok) {
        if (ms <= 0) {
            return 0;
        }
        if (ms >= 100 && ms <= 5000) {
            return ms;
        }
    }
    return 0;
}

/** 单人练习：摄像头挥拍事件最低置信（0–1） */
 float practiceCameraMinSwingScore()
{
    bool ok = false;
    const int pct = qEnvironmentVariableIntValue("WIDGET_PRACTICE_CAM_SWING_PERCENT", &ok);
    if (ok && pct >= 30 && pct <= 95) {
        return static_cast<float>(pct) / 100.0f;
    }
    return 0.48f;
}

/** 单人练习：摄像头稳定识别触发阈值（0–1） */
 float practiceCameraStableConf()
{
    bool ok = false;
    const int pct = qEnvironmentVariableIntValue("WIDGET_PRACTICE_CAM_STABLE_PERCENT", &ok);
    if (ok && pct >= 40 && pct <= 95) {
        return static_cast<float>(pct) / 100.0f;
    }
    return 0.52f;
}

// ═══════════ 颜色常量 (与 HTML CSS 完全一致) ═══════════
const char *COLOR_BG           = "#0b1424";
const char *COLOR_CARD_BG      = "#07172fb0";
const char *COLOR_CARD_BORDER  = "#234f8c";
const char *COLOR_TEXT_MAIN    = "#e7edf6";
const char *COLOR_TEXT_SUB     = "#b9cff1";
const char *COLOR_MUTED        = "#84a8d6";
const char *COLOR_ACCENT       = "#8cc7ff";
const char *COLOR_PRIMARY      = "#3aa7ff";
const char *COLOR_OK           = "#38d07e";
const char *COLOR_WARN         = "#ffb020";
const char *COLOR_BAD          = "#ff4d4d";
const char *COLOR_BTN_BORDER   = "#2e63ac";
const char *COLOR_BTN_BG_START = "#153563d9";
const char *COLOR_BTN_BG_END   = "#0f2649d2";
const char *COLOR_BTN_MAIN_START = "#1d5aa1";
const char *COLOR_BTN_MAIN_END   = "#123a6f";
const char *COLOR_BTN_HOVER    = "#56baff";
const char *COLOR_DANGER_START = "#5a1f1f";
const char *COLOR_DANGER_END   = "#3a1414";
const char *COLOR_DANGER_BORDER = "#7a2b2b";
const char *COLOR_DANGER_HOVER = "#ff6b6b";

/* 16:9 预览；单人练习（训练页）三栏中列尽量大，约 720p 视窗；对打/比赛中/班级用 kCamPreview* */
const int kTrainLeftColW = 448;
const int kCamTrainingW = 1344;
const int kCamTrainingH = kCamTrainingW * 9 / 16; /* 756 */
const int kCamPreviewW = 960;
const int kCamPreviewH = kCamPreviewW * 9 / 16; /* 540 */
const int kMatchLandingCamW = kCamTrainingW;
const int kMatchLandingCamH = kCamTrainingH;
const int kMatchStartBtnW = 960;
const int kMatchStartBtnH = 96;
/* 单人练习教学视频：素材为 1280×720 (16:9)，容器同比例以便铺满 */
const int kTutorialVideoW = 1024;
const int kTutorialVideoH = kTutorialVideoW * 9 / 16; /* 576 */
/* 动作详情页：尽量铺满右侧区域且不越界（1920×1080 扣 header/左栏/按钮） */
const int kSkillDetailLeftColW = 448;
const int kSkillDetailVideoW = 1280;
const int kSkillDetailVideoH = kSkillDetailVideoW * 9 / 16; /* 720 */
const int kSkillDetailVideoPad = 16;
const int kSkillDetailStartBtnW = 1160;
const int kActionReplayVideoW = 1280;
const int kActionReplayVideoH = kActionReplayVideoW * 9 / 16; /* 720 */
/* 首页 / 单人模式双卡按钮（相对原 248px 高约 1.28×） */
const int kModeCardMinH = 318;
const int kModeCardBoxMarginH = 46;
const int kModeCardBoxSpacing = 36;

QString replayBaseDir()
{
    const QByteArray v = qgetenv("WIDGET_REPLAY_DIR").trimmed();
    if (!v.isEmpty())
        return QString::fromUtf8(v);
    return QStringLiteral("/opt/widget_ui/replays");
}

QString replaySessionDir(const QString &sessionId)
{
    return replayBaseDir() + QLatin1Char('/') + sessionId;
}

QString replayHitMp4Path(const QString &sessionId, int hitIdx)
{
    return replaySessionDir(sessionId) + QStringLiteral("/hit_%1.mp4").arg(hitIdx);
}

QString replayHitFramesDir(const QString &sessionId, int hitIdx)
{
    return replaySessionDir(sessionId) + QStringLiteral("/hit_%1").arg(hitIdx);
}

QString resolveReplayClip(const QString &sessionId, int hitIdx)
{
    if (sessionId.isEmpty() || hitIdx <= 0)
        return QString();
    const QString mp4 = replayHitMp4Path(sessionId, hitIdx);
    if (QFileInfo::exists(mp4))
        return mp4;
    const QString dir = replayHitFramesDir(sessionId, hitIdx);
    if (QFileInfo::exists(dir + QStringLiteral("/done.flag"))) {
        if (QFileInfo::exists(mp4))
            return mp4;
        return dir;
    }
    const QDir framesDir(dir);
    if (framesDir.exists() && !framesDir.entryList(QStringList() << QStringLiteral("frame_*.ppm"), QDir::Files).isEmpty())
        return dir;
    return QString();
}

void publishReplaySession(const QString &sessionId)
{
    QFile f(QStringLiteral("/tmp/.widget_replay_session"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(sessionId.toUtf8());
        f.write("\n");
    }
}

void clearReplaySession()
{
    QFile::remove(QStringLiteral("/tmp/.widget_replay_session"));
}

void requestHitReplayCapture(const QString &sessionId, int hitIdx)
{
    if (sessionId.isEmpty() || hitIdx <= 0)
        return;
    QDir().mkpath(replaySessionDir(sessionId));
    QFile f(QStringLiteral("/tmp/.widget_replay_req"));
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        f.write(QStringLiteral("%1 %2\n").arg(sessionId).arg(hitIdx).toUtf8());
    }
}

QString formatPlaybackSpeedRate(double rate)
{
    if (qFuzzyCompare(rate, 1.0))
        return QStringLiteral("1.0");
    if (qFuzzyCompare(rate, 0.75))
        return QStringLiteral("0.75");
    if (rate == std::floor(rate))
        return QString::number(static_cast<int>(rate));
    return QString::number(rate);
}

const char *VIDEO_SPEED_BTN_NORMAL = R"(
    QPushButton {
        min-width: 104px; min-height: 56px; max-height: 56px; padding: 0 18px;
        border: 2px solid #2e63ac; border-radius: 16px;
        background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #12325a, stop:1 #0e2445);
        color: #eaf3ff; font-size: 24px; font-weight: 800;
    }
    QPushButton:hover { border-color: #56baff; }
)";
const char *VIDEO_SPEED_BTN_ACTIVE = R"(
    QPushButton {
        min-width: 104px; min-height: 56px; max-height: 56px; padding: 0 18px;
        border: 2px solid #56baff; border-radius: 16px;
        background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #1a4a82, stop:1 #12325a);
        color: #ffffff; font-size: 24px; font-weight: 800;
    }
)";

// ═══════════ 工具函数 ═══════════
QString cardStyle() {
    return QString("QFrame#card { border: 1px solid %1; border-radius: 16px; background: %2; }")
        .arg(COLOR_CARD_BORDER, COLOR_CARD_BG);
}

QPushButton *makePrimaryBtn(const QString &text) {
    auto *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QString(R"(
        QPushButton {
            border: 1px solid %1; border-radius: 18px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %2, stop:1 %3);
            color: #eaf3ff; font-size: 20px; font-weight: 800;
            padding: 0 22px;
        }
        QPushButton:hover { border-color: %4; }
    )").arg(COLOR_BTN_BORDER, COLOR_BTN_MAIN_START, COLOR_BTN_MAIN_END, COLOR_BTN_HOVER));
    return b;
}

QPushButton *makeModeCardBtn(const QString &title, const QString &subtitle)
{
    auto *btn = new QPushButton();
    btn->setMinimumHeight(kModeCardMinH);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setStyleSheet(QString(R"(
        QPushButton {
            border: 1px solid #2f6ab4; border-radius: 28px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %1, stop:1 %2);
            color: #eaf3ff; font-size: 27px; font-weight: 800; padding: 31px;
        }
        QPushButton:hover { border-color: %3; }
    )").arg(COLOR_BTN_MAIN_START, COLOR_BTN_MAIN_END, COLOR_BTN_HOVER));
    auto *lay = new QVBoxLayout(btn);
    lay->setAlignment(Qt::AlignCenter);
    auto *t1 = new QLabel(title);
    t1->setStyleSheet(QStringLiteral("font-size:44px; font-weight:900; background:transparent; color:#eaf3ff;"));
    t1->setAlignment(Qt::AlignCenter);
    lay->addWidget(t1);
    auto *t2 = new QLabel(subtitle);
    t2->setStyleSheet(QStringLiteral("font-size:22px; background:transparent; color:#9eb7de;"));
    t2->setAlignment(Qt::AlignCenter);
    t2->setWordWrap(true);
    lay->addWidget(t2);
    return btn;
}

QPushButton *makeGhostBtn(const QString &text) {
    auto *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QString(R"(
        QPushButton {
            border: 1px solid %1; border-radius: 16px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %2, stop:1 %3);
            color: #eaf3ff; font-size: 20px; font-weight: 800;
            padding: 14px 26px;
        }
        QPushButton:hover { border-color: %4; }
    )").arg(COLOR_BTN_BORDER, COLOR_BTN_BG_START, COLOR_BTN_BG_END, COLOR_BTN_HOVER));
    return b;
}

QPushButton *makeDangerBtn(const QString &text) {
    auto *b = new QPushButton(text);
    b->setCursor(Qt::PointingHandCursor);
    b->setStyleSheet(QString(R"(
        QPushButton {
            border: 1px solid %1; border-radius: 16px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 %2, stop:1 %3);
            color: #fff3f3; font-size: 21px; font-weight: 900;
        }
        QPushButton:hover { border-color: %4; }
    )").arg(COLOR_DANGER_BORDER, COLOR_DANGER_START, COLOR_DANGER_END, COLOR_DANGER_HOVER));
    return b;
}

QLabel *makeTitle1(const QString &text) {
    auto *l = new QLabel(text);
    l->setStyleSheet(QString("font-size: 36px; font-weight: 700; letter-spacing: 1px; color: %1; background: transparent;").arg(COLOR_TEXT_MAIN));
    return l;
}
QLabel *makeTitle2(const QString &text) {
    auto *l = new QLabel(text);
    l->setStyleSheet(QString("font-size: 17px; color: %1; background: transparent;").arg(COLOR_TEXT_SUB));
    return l;
}

QLabel *makeAccentLabel(const QString &text) {
    auto *l = new QLabel(text);
    l->setStyleSheet(QString("font-size: 12px; color: %1; background: transparent;").arg(COLOR_ACCENT));
    return l;
}

 QWidget *makeHeader(const QString &title, const QString &subtitle, const QString &badge,
                    QPushButton *backBtn, QPushButton *homeBtn) {
    auto *w = new QWidget();
    w->setStyleSheet("background: transparent;");
    auto *lay = new QHBoxLayout(w);
    lay->setContentsMargins(14, 10, 14, 10);
    auto *left = new QVBoxLayout();
    left->setSpacing(4);
    left->addWidget(makeTitle1(title));
    if (!subtitle.isEmpty()) left->addWidget(makeTitle2(subtitle));
    lay->addLayout(left);
    lay->addStretch();
    if (backBtn) { backBtn->setStyleSheet(QString("font-size: 12px; color: %1; border: 1px solid #2a4f85; border-radius: 999px; padding: 8px 14px; background: #0a1a3559;").arg(COLOR_ACCENT)); backBtn->setCursor(Qt::PointingHandCursor); lay->addWidget(backBtn); }
    if (homeBtn) { homeBtn->setStyleSheet(QString("font-size: 12px; color: %1; border: 1px solid #2a4f85; border-radius: 999px; padding: 8px 14px; background: #0a1a3559;").arg(COLOR_ACCENT)); homeBtn->setCursor(Qt::PointingHandCursor); lay->addWidget(homeBtn); }
    if (!badge.isEmpty()) {
        auto *b = new QLabel(badge);
        b->setStyleSheet(QString("font-size: 12px; color: %1; border: 1px solid #2a4f85; border-radius: 999px; padding: 8px 14px; background: #0a1a3559;").arg(COLOR_ACCENT));
        lay->addWidget(b);
    }
    auto *sep = new QWidget();
    sep->setFixedHeight(1);
    sep->setStyleSheet("background: #1a2f50; margin: 0 14px;");
    sep->setParent(w);
    return w;
}

QWidget *makeCard() {
    auto *f = new QFrame();
    f->setObjectName("card");
    f->setStyleSheet(cardStyle());
    return f;
}

QWidget *makeCardWithLayout(QVBoxLayout *&outLay) {
    auto *f = makeCard();
    auto *l = new QVBoxLayout(f);
    l->setContentsMargins(16, 14, 16, 14);
    l->setSpacing(10);
    outLay = l;
    return f;
}

// ═══════════ 全局样式 ═══════════
const char *GLOBAL_STYLE = R"(
QWidget { color: #e7edf6; font-size: 14px; }
QStackedWidget#mainStack { background: transparent; }
QStackedWidget#mainStack > QWidget { background: transparent; }
QPushButton { border: none; }
QLabel { background: transparent; }
QPushButton#ghostBtn {
    font-size: 20px;
    font-weight: 800;
    color: #8cc7ff;
    border: 1px solid #2a4f85;
    border-radius: 999px;
    padding: 16px 32px;
    min-height: 56px;
    background: #0a1a3559;
}
QPushButton#ghostBtn:hover { border-color: #56baff; }
)";

SidebarWidget::SidebarWidget(const QStringList &items, const QString &connectTitle,
                           QLineEdit *&codeInput, QLabel *&msgLabel, QPushButton *&connectBtn,
                           QWidget *parent)
    : QWidget(parent)
{
        setFixedWidth(270);
        setStyleSheet("background: #0a1422e6; border-right: 1px solid #1a2f50;");
        auto *lay = new QVBoxLayout(this);
        lay->setContentsMargins(14, 18, 14, 18);
        lay->setSpacing(10);

        for (const auto &item : items) {
            auto *btn = new QPushButton(item);
            btn->setCursor(Qt::PointingHandCursor);
            btn->setStyleSheet(QString(R"(
                QPushButton {
                    border: 1px solid #2e63ac; border-radius: 12px;
                    background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #153563d9, stop:1 #0f2649d2);
                    color: #eaf3ff; font-size: 17px; font-weight: 700; padding: 14px;
                    text-align: left;
                }
                QPushButton:hover { border-color: #56baff; }
            )"));
            lay->addWidget(btn);
        }

        lay->addStretch();

        auto *sep = new QFrame();
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet("color: #1a2f50;");
        lay->addWidget(sep);

        auto *ct = new QLabel(connectTitle);
        ct->setStyleSheet(QString("font-size: 14px; color: %1; font-weight: 700; background: transparent;").arg(COLOR_ACCENT));
        lay->addWidget(ct);

        codeInput = new QLineEdit();
        codeInput->setPlaceholderText("输入设备码");
        codeInput->setStyleSheet(QString(R"(
            QLineEdit {
                border: 1px solid %1; border-radius: 10px;
                background: #0c1b35; color: #eaf3ff; padding: 10px 14px; font-size: 15px;
            }
            QLineEdit:focus { border-color: #56baff; }
        )").arg(COLOR_BTN_BORDER));
        lay->addWidget(codeInput);

        msgLabel = new QLabel("");
        msgLabel->setStyleSheet("font-size: 13px; color: #84a8d6; background: transparent;");
        msgLabel->setWordWrap(true);
        lay->addWidget(msgLabel);

        connectBtn = makePrimaryBtn("连接");
        connectBtn->setFixedHeight(48);
        lay->addWidget(connectBtn);
}

// ═══════════════════════════════════════════════
// PageBase 实现
// ═══════════════════════════════════════════════
PageBase::PageBase(const QString &title, const QString &subtitle, MainWindow *mw, QWidget *parent,
                   PageHeaderMode headerMode)
    : QWidget(parent), m_mainWindow(mw)
{
    m_rootLayout = new QVBoxLayout(this);
    m_rootLayout->setContentsMargins(0, 0, 0, 0);
    m_rootLayout->setSpacing(0);

    // Header
    auto *header = new QWidget();
    header->setObjectName("header");
    header->setFixedHeight(84);
    auto *hLay = new QHBoxLayout(header);
    hLay->setContentsMargins(16, 0, 16, 0);
    hLay->setSpacing(12);

    m_backBtn = new QPushButton("返回上级");
    m_backBtn->setObjectName("ghostBtn");
    m_backBtn->setCursor(Qt::PointingHandCursor);
    m_backBtn->hide();

    m_homeBtn = new QPushButton("返回首页");
    m_homeBtn->setObjectName("ghostBtn");
    m_homeBtn->setCursor(Qt::PointingHandCursor);

    m_backBtn->setMinimumHeight(56);
    m_homeBtn->setMinimumHeight(56);

    m_titleLabel = new QLabel(title);
    if (!subtitle.isEmpty()) {
        m_subtitleLabel = new QLabel(subtitle);
        m_subtitleLabel->setStyleSheet("font-size:15px; color:#9bb0c9; background:transparent;");
    } else {
        m_subtitleLabel = nullptr;
    }

    if (headerMode == PageHeaderMode::SingleCentered) {
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setStyleSheet("font-size:30px; font-weight:800; color:#eaf3ff; background:transparent;");
        hLay->addStretch(1);
        if (m_subtitleLabel) {
            m_subtitleLabel->setAlignment(Qt::AlignCenter);
            auto *titleWrap = new QWidget();
            auto *tb = new QVBoxLayout(titleWrap);
            tb->setContentsMargins(0, 0, 0, 0);
            tb->setSpacing(2);
            tb->addWidget(m_titleLabel);
            tb->addWidget(m_subtitleLabel);
            hLay->addWidget(titleWrap, 1);
        } else {
            hLay->addWidget(m_titleLabel, 1);
        }
        hLay->addStretch(1);
        hLay->addWidget(m_backBtn);
        hLay->addWidget(m_homeBtn);
    } else {
        m_titleLabel->setStyleSheet("font-size:23px; font-weight:800; background:transparent;");
        auto *titleBox = new QVBoxLayout();
        titleBox->setSpacing(2);
        titleBox->addWidget(m_titleLabel);
        if (m_subtitleLabel)
            titleBox->addWidget(m_subtitleLabel);
        hLay->addLayout(titleBox);
        hLay->addStretch();
        hLay->addWidget(m_backBtn);
        hLay->addWidget(m_homeBtn);
    }

    m_rootLayout->addWidget(header);
}

void PageBase::setBackTarget(const QString &backLabel, const QString & /*backPage*/) {
    m_backBtn->setText(backLabel);
    m_backBtn->show();
}

#include "ui_pages.moc"
