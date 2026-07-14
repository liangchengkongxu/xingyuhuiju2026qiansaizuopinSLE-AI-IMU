#ifndef UI_COMMON_H
#define UI_COMMON_H

#include <QString>
#include <QSize>
#include <QPushButton>
#include <QLabel>
#include <QWidget>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QFrame>

class MainWindow;

extern const int kTrainLeftColW;
extern const int kCamTrainingW;
extern const int kCamTrainingH;
extern const int kCamPreviewW;
extern const int kCamPreviewH;
extern const int kMatchLandingCamW;
extern const int kMatchLandingCamH;
extern const int kMatchStartBtnW;
extern const int kMatchStartBtnH;
extern const int kTutorialVideoW;
extern const int kTutorialVideoH;
extern const int kSkillDetailLeftColW;
extern const int kSkillDetailVideoW;
extern const int kSkillDetailVideoH;
extern const int kSkillDetailVideoPad;
extern const int kSkillDetailStartBtnW;
extern const int kActionReplayVideoW;
extern const int kActionReplayVideoH;
extern const int kModeCardMinH;
extern const int kModeCardBoxMarginH;
extern const int kModeCardBoxSpacing;

extern const char *COLOR_BG;
extern const char *COLOR_CARD_BG;
extern const char *COLOR_CARD_BORDER;
extern const char *COLOR_TEXT_MAIN;
extern const char *COLOR_TEXT_SUB;
extern const char *COLOR_MUTED;
extern const char *COLOR_ACCENT;
extern const char *COLOR_PRIMARY;
extern const char *COLOR_OK;
extern const char *COLOR_WARN;
extern const char *COLOR_BAD;
extern const char *COLOR_BTN_BORDER;
extern const char *COLOR_BTN_BG_START;
extern const char *COLOR_BTN_BG_END;
extern const char *COLOR_BTN_MAIN_START;
extern const char *COLOR_BTN_MAIN_END;
extern const char *COLOR_BTN_HOVER;
extern const char *COLOR_DANGER_START;
extern const char *COLOR_DANGER_END;
extern const char *COLOR_DANGER_BORDER;
extern const char *COLOR_DANGER_HOVER;

extern const char *VIDEO_SPEED_BTN_NORMAL;
extern const char *VIDEO_SPEED_BTN_ACTIVE;
extern const char *GLOBAL_STYLE;

QSize linuxFbSizeFromPlatformEnv();
int randInt(int lo, int hi);
int displaySpeedKmh(double speedKmh);
bool widgetUseCameraHits();
bool widgetUseImuHits();
int practiceHitCooldownMs();
float practiceCameraMinSwingScore();
float practiceCameraStableConf();

QString replayBaseDir();
QString replaySessionDir(const QString &sessionId);
QString replayHitMp4Path(const QString &sessionId, int hitIdx);
QString replayHitFramesDir(const QString &sessionId, int hitIdx);
QString resolveReplayClip(const QString &sessionId, int hitIdx);
int replayPoseEagerMax();
bool replayHitNeedsPoseRender(const QString &sessionId, int hitIdx);
void publishReplaySession(const QString &sessionId);
void clearReplaySession();
void requestHitReplayCapture(const QString &sessionId, int hitIdx);
void requestHitReplayPoseRender(const QString &sessionId, int hitIdx);
QString formatPlaybackSpeedRate(double rate);

QString cardStyle();
QPushButton *makePrimaryBtn(const QString &text);
QPushButton *makeModeCardBtn(const QString &title, const QString &subtitle);
QPushButton *makeGhostBtn(const QString &text);
QPushButton *makeDangerBtn(const QString &text);
QLabel *makeTitle1(const QString &text);
QLabel *makeTitle2(const QString &text);
QLabel *makeAccentLabel(const QString &text);
QWidget *makeHeader(const QString &title, const QString &subtitle, const QString &badge,
                    QPushButton *backBtn = nullptr, QPushButton *homeBtn = nullptr);
QWidget *makeCard();
QWidget *makeCardWithLayout(QVBoxLayout *&outLay);

#endif
