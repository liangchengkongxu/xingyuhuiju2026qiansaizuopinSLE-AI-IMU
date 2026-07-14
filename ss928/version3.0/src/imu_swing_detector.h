#ifndef IMU_SWING_DETECTOR_H
#define IMU_SWING_DETECTOR_H

#include <QString>
#include <optional>

struct ImuSample {
    int tMs = 0;
    double axG = 0;
    double ayG = 0;
    double azG = 0;
    double gxDps = 0;
    double gyDps = 0;
    double rollDeg = 0;
    double pitchDeg = 0;
    double mG = 0;
    /* 1D CNN 原始通道：mg / dps / deg / g，与 deploy/badminton_preprocess 一致 */
    float rawCh[8] = {0};

    double gyroMag() const;
    double accelMag() const;
};

struct SwingEstimate {
    double speedKmh = 0;
    int powerTen = 1;
    double peakDynG = 0;
    double peakGyro = 0;
    double peakMG = 0;
    double peakAccelMagG = 0;
    double impulseDynGs = 0;
    int durationMs = 0;
    int swingT0Ms = 0;
    int swingTEndMs = 0;
};

/** 挥拍窗口统计量；impulseDynGs / peakAccelMagG 为 0 表示不可用（如 CNN 仅峰顶路径） */
struct SwingMetrics {
    double peakDynG = 0;
    double peakGyroDps = 0;
    double peakMG = 0;
    int durationMs = 0;
    double impulseDynGs = 0;
    double peakAccelMagG = 0;
};

double estimateSpeedKmh(const SwingMetrics &m);
int estimatePowerTen(const SwingMetrics &m);

/** 兼容旧接口 */
int estimatePowerTen(double peakDynG, double peakGyroDps, double peakMG);

/**
 * 班级/多人 IMU 挥拍评分：50 分保底，其余最多 49 分由 CNN 匹配度 + 动作适宜力度构成。
 * strokeClassId: 0高远 1平抽 2挑球 3放网 4发球 5杀球，未知传 -1。
 */
int classHitScoreFromImu(int strokeClassId, const QString &hitTypeLabel, float strokeConfidence, int powerTen);

class ImuSwingDetector {
public:
    void reset();
    std::optional<SwingEstimate> feed(const ImuSample &s);

private:
    void updateBaseline(const ImuSample &s, double dyn);
    void accumulateSwingSample(const ImuSample &s, double dyn);
    bool swingTrigger(double dyn, double gyroDps, double mStep) const;
    bool isLargeBurst() const;
    double dynG(const ImuSample &s) const;
    double effectiveGyroDps(const ImuSample &s) const;
    std::optional<SwingEstimate> finish(int tEnd);

    double baselineM_ = 1.0;
    const char *state_ = "idle";
    bool armed_ = true;
    int onConfirm_ = 0;
    int swingT0_ = 0;
    int lastFinishT_ = 0;
    double peakDyn_ = 0;
    double peakGyro_ = 0;
    double peakM_ = 0;
    double peakAccelMag_ = 0;
    double impulseDyn_ = 0;
    int lastSwingSampleT_ = 0;
    double lastRollDeg_ = 0;
    double lastPitchDeg_ = 0;
    bool hasOrientRef_ = false;
    double prevMG_ = 0;
    double prevRollTrack_ = 0;
    double prevPitchTrack_ = 0;
    int prevSampleT_ = 0;
};

bool parseImuLine(const QString &line, ImuSample *out);
bool parseImuTaggedLine(const QString &line, ImuSample *out, QString *macOut = nullptr);

#endif
