#ifndef IMU_CNN_CLASSIFIER_H
#define IMU_CNN_CLASSIFIER_H

#include <QHash>
#include <QString>
#include <optional>

#include "badminton_npu_infer.h"
#include "imu_swing_detector.h"

struct ImuStrokeResult {
    int classId = -1;
    float confidence = 0.0f;
    QString labelCn;
};

/** CNN 触发击球：含峰值时刻与窗口，供球速/力度估算 */
struct ImuCnnHitEvent {
    ImuStrokeResult stroke;
    int peakTMs = 0;
    int swingT0Ms = 0;
    int swingTEndMs = 0;
    double peakMG = 0;
    double peakGyro = 0;
    float probs[BADMINTON_NPU_NUM_CLASSES] = {0};
};

class ImuCnnClassifier {
public:
    ImuCnnClassifier();
    ~ImuCnnClassifier();

    bool init(const QString &omPath);
    void resetMac(const QString &mac);
    void resetAll();
    void pushSample(const QString &mac, const ImuSample &sample);
    /** 规则挥拍检测完成后，对窗口做击球类型分类；minConfOverride<0 时用 WIDGET_IMU_CNN_CLASS_CONF */
    std::optional<ImuStrokeResult> classifySwing(const QString &mac, int swingT0Ms, int swingTEndMs,
        float minConfOverride = -1.0f);
    /** 以 M 局部峰值 + 24 帧窗口跑 1D CNN，置信度够则触发击球（不依赖规则 FSM） */
    std::optional<ImuCnnHitEvent> tryDetectHit(const QString &mac);
    /** CNN 路径近期是否已触发（both 模式下避免规则 FSM 重复计数） */
    bool hadRecentHit(const QString &mac, int tMs, int cooldownMs) const;

private:
    struct RingEntry {
        int tMs = 0;
        float ch[8] = {0};
        float mG = 0;
        float gyroDps = 0;
    };

    struct MacState {
        QVector<RingEntry> ring;
        int lastHitTMs = 0;
        int lastProcessedPeakTMs = -1;
        bool armed = true;
    };

    bool ready_ = false;
    QHash<QString, MacState> states_;

    static QString normMac(const QString &mac);
    bool extractWindow(const MacState &st, int centerIdx, float out[8][24]) const;
};

#endif
