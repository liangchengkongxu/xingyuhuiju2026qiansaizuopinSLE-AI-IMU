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
// 首页 实现
// ═══════════════════════════════════════════════
HomePage::HomePage(MainWindow *mw, QWidget *parent) : QWidget(parent) {
    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    setObjectName("homePage");
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet("#homePage { background: transparent; }");

    // Header
    auto *header = new QWidget();
    header->setObjectName("header");
    header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto *hLay = new QVBoxLayout(header);
    hLay->setContentsMargins(40, 56, 40, 20);
    hLay->setSpacing(4);
    hLay->setAlignment(Qt::AlignCenter);
    auto *title = new QLabel("星羽汇聚");
    title->setStyleSheet("font-size:56px; font-weight:900; background:transparent; color:#eaf3ff;");
    title->setAlignment(Qt::AlignCenter);
    hLay->addWidget(title);
    auto *sub = new QLabel("欢迎进入训练中心");
    sub->setStyleSheet("font-size:24px; color:#9eb7de; background:transparent;");
    sub->setAlignment(Qt::AlignCenter);
    hLay->addWidget(sub);
    auto *status = new QLabel("SS928 · openEuler");
    status->setStyleSheet("font-size:14px; color:#8cc7ff; background:transparent; margin-top:6px;");
    status->setAlignment(Qt::AlignCenter);
    hLay->addWidget(status);
    lay->addWidget(header);

    lay->addStretch();

    // 两个模式按钮
    auto *btnBox = new QHBoxLayout();
    btnBox->setContentsMargins(kModeCardBoxMarginH, 0, kModeCardBoxMarginH, 0);
    btnBox->setSpacing(kModeCardBoxSpacing);
    btnBox->addStretch();

    auto *singleBtn = makeModeCardBtn(QStringLiteral("单人/对打模式"),
        QStringLiteral("适合个人专项练习，支持发球、步伐与击球节奏训练。"));
    connect(singleBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(1); });
    btnBox->addWidget(singleBtn);

    auto *multiBtn = makeModeCardBtn(QStringLiteral("班级模式"),
        QStringLiteral("适合多人同训与课堂教学，可进行分组和统一训练计划。"));
    connect(multiBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(10); });
    btnBox->addWidget(multiBtn);

    btnBox->addStretch();
    lay->addLayout(btnBox);
    lay->addStretch();

    // 底部
    auto *foot = new QLabel("首屏原型 v2");
    foot->setStyleSheet("font-size:13px; color:#556; background:transparent; padding:18px;");
    foot->setAlignment(Qt::AlignCenter);
    lay->addWidget(foot);
}

// ═══════════════════════════════════════════════
// 单人模式 实现
// ═══════════════════════════════════════════════
SinglePage::SinglePage(MainWindow *mw, QWidget *parent)
    : PageBase(QStringLiteral("单人/对打模式"), "", mw, parent)
{
    connect(m_backBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();

    m_rootLayout->addStretch();

    auto *btnBox = new QHBoxLayout();
    btnBox->setContentsMargins(kModeCardBoxMarginH, 0, kModeCardBoxMarginH, 0);
    btnBox->setSpacing(kModeCardBoxSpacing);
    btnBox->addStretch();

    auto *practiceBtn = makeModeCardBtn(QStringLiteral("单人练习模式"),
        QStringLiteral("选择技能、观看教学视频，进行专项动作练习。"));
    connect(practiceBtn, &QPushButton::clicked, mw, [mw]() {
        if (mw->m_singlePracticeSetup)
            mw->m_singlePracticeSetup->resetScan();
        mw->switchPage(14);
    });
    btnBox->addWidget(practiceBtn);

    auto *matchBtn = makeModeCardBtn(QStringLiteral("对打 / 比赛模式"),
        QStringLiteral("双人对打或比赛计分，实时预览与动作识别。"));
    connect(matchBtn, &QPushButton::clicked, mw, [mw]() {
        if (mw->m_matchSetup)
            mw->m_matchSetup->resetScan();
        mw->switchPage(16);
    });
    btnBox->addWidget(matchBtn);

    btnBox->addStretch();
    m_rootLayout->addLayout(btnBox);
    m_rootLayout->addStretch();
}

// ═══════════════════════════════════════════════
// 练习模式 实现
// ═══════════════════════════════════════════════
PracticePage::PracticePage(MainWindow *mw, QWidget *parent)
    : PageBase(QStringLiteral("单人练习模式"), "", mw, parent)
{
    connect(m_backBtn, &QPushButton::clicked, mw, [mw]() { mw->switchPage(1); });
    connect(m_homeBtn, &QPushButton::clicked, mw, [mw]() { mw->goHome(); });
    m_backBtn->show();

    m_rootLayout->addStretch();

    auto *content = new QWidget();
    content->setStyleSheet("background:transparent;");
    auto *cl = new QVBoxLayout(content);
    cl->setContentsMargins(48, 0, 48, 0);
    cl->setSpacing(28);

    auto *tip = new QLabel(QStringLiteral("选择基础动作，进入专项练习："));
    tip->setStyleSheet("font-size: 22px; color: #9eb7de; background: transparent;");
    tip->setAlignment(Qt::AlignCenter);
    cl->addWidget(tip);

    const QString btnStyle = R"(
        QPushButton {
            border: 1px solid #2e63ac; border-radius: 20px;
            background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 #153563, stop:1 #0f2649);
            color: #eaf3ff; font-size: 34px; font-weight: 800;
            padding: 20px 16px;
        }
        QPushButton:hover { border-color: #56baff; }
    )";

    QStringList skills = {QStringLiteral("杀球"), QStringLiteral("放网"), QStringLiteral("高远"),
        QStringLiteral("平抽"), QStringLiteral("挑球")};

    auto makeSkillBtn = [&](const QString &name) {
        auto *btn = new QPushButton(name);
        btn->setMinimumHeight(148);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(btnStyle);
        connect(btn, &QPushButton::clicked, this, [this, mw, name]() {
            currentSkill = name;
            emit skillSelected(name);
            mw->switchPage(3);
        });
        return btn;
    };

    auto *row1 = new QHBoxLayout();
    row1->setSpacing(24);
    for (int i = 0; i < 3; ++i)
        row1->addWidget(makeSkillBtn(skills[i]), 1);
    cl->addLayout(row1);

    auto *row2 = new QHBoxLayout();
    row2->setSpacing(24);
    row2->addStretch(1);
    row2->addWidget(makeSkillBtn(skills[3]), 2);
    row2->addWidget(makeSkillBtn(skills[4]), 2);
    row2->addStretch(1);
    cl->addLayout(row2);

    m_rootLayout->addWidget(content);
    m_rootLayout->addStretch();
}
