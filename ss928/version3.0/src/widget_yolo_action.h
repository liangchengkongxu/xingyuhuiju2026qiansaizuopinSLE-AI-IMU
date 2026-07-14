#ifndef WIDGET_YOLO_ACTION_H
#define WIDGET_YOLO_ACTION_H

#include <QObject>
#include <QString>
#include <QList>
#include <QtGlobal>

class QTimer;

struct WidgetSwingEvent {
    int seq = 0;
    int clsId = -1;
    float score = 0.0f;
    qint64 tsMs = 0;
};

/** 轮询 sample_vio_ai：动作状态 + 挥拍事件队列（与 cam-swing 日志 1:1） */
class WidgetYoloActionService : public QObject {
    Q_OBJECT
public:
    explicit WidgetYoloActionService(QObject *parent = nullptr);

    int clsId() const { return m_clsId; }
    float score() const { return m_score; }
    bool isStable() const { return m_stable; }
    QString nameCn() const { return m_nameCn; }
    QString nameEn() const { return m_nameEn; }
    bool hasDetection() const { return m_hasDetection; }
    /** 与 grep cam-swing 累计序号一致（自 AI 进程启动） */
    int totalSwingSeq() const { return m_swingSeq; }
    /** 本次训练/比赛会话内挥拍次数（与日志新增行数一致） */
    int sessionSwingCount() const { return m_sessionCount; }

    QString lastStableNameCn() const { return m_lastStableNameCn; }
    int lastStableClsId() const { return m_lastStableClsId; }

    static QString classNameCn(int clsId);
    static QString classNameEn(int clsId);

    /** 在 [centerMs-halfWindowMs, centerMs+halfWindowMs] 内取置信度最高的视觉挥拍 */
    bool bestSwingInWindow(qint64 centerMs, qint64 halfWindowMs, int &outClsId, float &outScore,
                           QString &outNameCn) const;

public slots:
    void start();
    void stop();
    /** 开始训练/比赛时调用：只统计此后的挥拍，与清空后日志对齐 */
    void resetSessionBaseline();

signals:
    void actionUpdated(int clsId, const QString &nameCn, const QString &nameEn, float score, bool stable);
    void swingDetected(int swingSeq, int clsId, const QString &nameCn, float score);
    /** 会话内挥拍次数变化（界面直接显示此值） */
    void swingCountChanged(int sessionCount);

private slots:
    void pollActionFile();

private:
    void pollSwingEvents();
    void appendSwingHistory(int seq, int clsId, float score, qint64 tsMs);
    void trimSwingHistory();

    QTimer *m_poll = nullptr;
    qint64 m_eventsOffset = 0;
    int m_sessionCount = 0;
    int m_clsId = -1;
    float m_score = 0.0f;
    bool m_stable = false;
    bool m_hasDetection = false;
    QString m_nameCn;
    QString m_nameEn;
    QString m_lastStableNameCn;
    int m_lastStableClsId = -1;
    int m_swingSeq = 0;
    QList<WidgetSwingEvent> m_swingHistory;
};

#endif
