#include "imu_swing_detector.h"

#include <QRegularExpression>
#include <QString>
#include <QtGlobal>
#include <cmath>

namespace {

/* paibing_imu Notify 间隔约 100ms（10Hz），时间类参数按此标定 */
constexpr int kImuSampleMs = 100;
constexpr double kSwingOffG = 0.05;
constexpr double kBaselineUpdateMaxDynG = 0.09;
constexpr int kSwingMinMs = kImuSampleMs / 3;
constexpr int kSwingMaxMs = 650;
constexpr double kG0 = 9.80665;
constexpr double kTipGain = 3.2;
constexpr double kTEff = 0.18;
constexpr double kGyroScale = 0.0065;
constexpr double kKmPerDynG = 95.0;
constexpr double kImpulseGain = 2.75;
constexpr double kPeakRateScale = 1.35;
constexpr double kBaselineAlpha = 0.005;

double ruleSwingOnDynG()
{
    bool ok = false;
    const int centiG = qEnvironmentVariableIntValue("WIDGET_IMU_RULE_SWING_ON_DYN", &ok);
    if (ok && centiG >= 1 && centiG <= 30) {
        return static_cast<double>(centiG) / 100.0;
    }
    return 0.06;
}

double ruleMinPeakDynG()
{
    bool ok = false;
    const int centiG = qEnvironmentVariableIntValue("WIDGET_IMU_RULE_MIN_PEAK_DYN", &ok);
    if (ok && centiG >= 1 && centiG <= 30) {
        return static_cast<double>(centiG) / 100.0;
    }
    return 0.06;
}

double ruleGyroOnDps()
{
    bool ok = false;
    const int dps = qEnvironmentVariableIntValue("WIDGET_IMU_RULE_GYRO_ON", &ok);
    if (ok && dps >= 5 && dps <= 200) {
        return static_cast<double>(dps);
    }
    return 35.0;
}

double ruleMinPeakGyroDps()
{
    bool ok = false;
    const int dps = qEnvironmentVariableIntValue("WIDGET_IMU_RULE_MIN_PEAK_GYRO", &ok);
    if (ok && dps >= 8 && dps <= 200) {
        return static_cast<double>(dps);
    }
    return 28.0;
}

double ruleGyroAssistMinDynG()
{
    return qMax(0.005, ruleSwingOnDynG() * 0.35);
}

int ruleConfirmSamples()
{
    bool ok = false;
    const int n = qEnvironmentVariableIntValue("WIDGET_IMU_RULE_CONFIRM", &ok);
    if (ok && n >= 1 && n <= 3) {
        return n;
    }
    return 1;
}

int ruleCooldownMs()
{
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("WIDGET_IMU_RULE_COOLDOWN_MS", &ok);
    if (ok && ms >= 0 && ms <= 2000) {
        return ms;
    }
    return 300;
}

double ruleBurstMinDynG()
{
    bool ok = false;
    const int centiG = qEnvironmentVariableIntValue("WIDGET_IMU_BURST_MIN_DYN", &ok);
    if (ok && centiG >= 1 && centiG <= 40) {
        return static_cast<double>(centiG) / 100.0;
    }
    return 0.07;
}

double ruleBurstMinGyroDps()
{
    bool ok = false;
    const int dps = qEnvironmentVariableIntValue("WIDGET_IMU_BURST_MIN_GYRO", &ok);
    if (ok && dps >= 15 && dps <= 250) {
        return static_cast<double>(dps);
    }
    return 45.0;
}

} // namespace

void ImuSwingDetector::reset()
{
    baselineM_ = 1.0;
    state_ = "idle";
    armed_ = true;
    onConfirm_ = 0;
    swingT0_ = 0;
    lastFinishT_ = 0;
    peakDyn_ = 0;
    peakGyro_ = 0;
    peakM_ = 0;
    peakAccelMag_ = 0;
    impulseDyn_ = 0;
    lastSwingSampleT_ = 0;
    lastRollDeg_ = 0;
    lastPitchDeg_ = 0;
    hasOrientRef_ = false;
    prevMG_ = 0;
    prevRollTrack_ = 0;
    prevPitchTrack_ = 0;
    prevSampleT_ = 0;
}

void ImuSwingDetector::updateBaseline(const ImuSample &s, double dyn)
{
    if (dyn < kBaselineUpdateMaxDynG)
        baselineM_ = (1.0 - kBaselineAlpha) * baselineM_ + kBaselineAlpha * s.mG;
}

bool ImuSwingDetector::swingTrigger(double dyn, double gyroDps, double mStep) const
{
    if (!armed_)
        return false;
    const double swingOn = ruleSwingOnDynG();
    const double gyroOn = ruleGyroOnDps();
    const double gyroAssistDyn = ruleGyroAssistMinDynG();
    const bool accTrigger = dyn >= swingOn;
    const bool gyroTrigger = gyroDps >= gyroOn && dyn >= gyroAssistDyn;
    /* 10Hz 下单帧 M 跳变：需更大跳变且伴随陀螺或更强 dyn，避免手持微动误触 */
    const bool stepTrigger = mStep >= swingOn * 1.05
        && (gyroDps >= gyroOn * 0.35 || dyn >= swingOn * 1.15);
    return accTrigger || gyroTrigger || stepTrigger;
}

double ImuSwingDetector::effectiveGyroDps(const ImuSample &s) const
{
    const double hw = s.gyroMag();
    if (hw >= 1.0) {
        return hw;
    }
    if (prevSampleT_ <= 0 || s.tMs <= prevSampleT_) {
        return hw;
    }
    const double dt = (s.tMs - prevSampleT_) / 1000.0;
    if (dt < 0.04 || dt > 0.35) {
        return hw;
    }
    const double orientRate =
        (std::abs(s.rollDeg - prevRollTrack_) + std::abs(s.pitchDeg - prevPitchTrack_)) / dt;
    return qMax(hw, orientRate);
}

bool ImuSwingDetector::isLargeBurst() const
{
    const double burstDyn = ruleBurstMinDynG();
    const double burstGyro = ruleBurstMinGyroDps();
    if (peakDyn_ >= burstDyn || peakGyro_ >= burstGyro) {
        return true;
    }
    return peakDyn_ >= burstDyn * 0.72 && peakGyro_ >= burstGyro * 0.72;
}

double ImuSample::gyroMag() const
{
    return std::sqrt(gxDps * gxDps + gyDps * gyDps);
}

double ImuSample::accelMag() const
{
    return std::sqrt(axG * axG + ayG * ayG + azG * azG);
}

bool parseImuLine(const QString &line, ImuSample *out)
{
    if (!out)
        return false;
    QString s = line.trimmed();
    const int at = s.indexOf(QLatin1Char('@'));
    if (at < 0)
        return false;
    if (at > 0)
        s = s.mid(at);

    static const QRegularExpression reWithGz(
        QStringLiteral(
            R"(@(\d+),A([+-]?\d+),([+-]?\d+),([+-]?\d+),G([+-]?\d+),([+-]?\d+),([+-]?\d+),R([+-]?\d+),P([+-]?\d+),M(\d+))"));
    static const QRegularExpression reLegacy(
        QStringLiteral(
            R"(@(\d+),A([+-]?\d+),([+-]?\d+),([+-]?\d+),G([+-]?\d+),([+-]?\d+),R([+-]?\d+),P([+-]?\d+),M(\d+))"));
    QRegularExpressionMatch m = reWithGz.match(s);
    const bool hasGz = m.hasMatch();
    if (!hasGz) {
        m = reLegacy.match(s);
        if (!m.hasMatch()) {
            return false;
        }
    }

    ImuSample sample;
    sample.tMs = m.captured(1).toInt();
    int axMg = m.captured(2).toInt();
    int ayMg = m.captured(3).toInt();
    int azMg = m.captured(4).toInt();
    /* 新 SLE ASCII / 蓝牙 Notify：A* 为 centi-g；旧二进制转 ASCII 为 mg */
    if (std::abs(axMg) <= 300 && std::abs(ayMg) <= 300 && std::abs(azMg) <= 300) {
        axMg *= 10;
        ayMg *= 10;
        azMg *= 10;
    }
    const int gxDps = m.captured(5).toInt();
    const int gyDps = m.captured(6).toInt();
    const int gzDps = hasGz ? m.captured(7).toInt() : 0;
    const int rollTenth = m.captured(hasGz ? 8 : 7).toInt();
    const int pitchTenth = m.captured(hasGz ? 9 : 8).toInt();
    const int mRaw = m.captured(hasGz ? 10 : 9).toInt();

    sample.axG = axMg / 1000.0;
    sample.ayG = ayMg / 1000.0;
    sample.azG = azMg / 1000.0;
    sample.gxDps = gxDps;
    sample.gyDps = gyDps;
    sample.rollDeg = rollTenth / 10.0;
    sample.pitchDeg = pitchTenth / 10.0;
    sample.mG = mRaw / 100.0;
    sample.rawCh[0] = static_cast<float>(axMg);
    sample.rawCh[1] = static_cast<float>(ayMg);
    sample.rawCh[2] = static_cast<float>(azMg);
    sample.rawCh[3] = static_cast<float>(gxDps);
    sample.rawCh[4] = static_cast<float>(gyDps);
    sample.rawCh[5] = static_cast<float>(hasGz ? gzDps : pitchTenth) / (hasGz ? 1.0f : 10.0f);
    sample.rawCh[6] = static_cast<float>(rollTenth) / 10.0f;
    sample.rawCh[7] = static_cast<float>(mRaw) / 100.0f;

    *out = sample;
    return true;
}

bool parseImuTaggedLine(const QString &line, ImuSample *out, QString *macOut)
{
    QString body = line.trimmed();
    if (body.startsWith(QStringLiteral("mac="))) {
        const int pipe = body.indexOf(QLatin1Char('|'));
        if (pipe > 4) {
            if (macOut)
                *macOut = body.mid(4, pipe - 4).trimmed();
            body = body.mid(pipe + 1).trimmed();
        }
    }
    return parseImuLine(body, out);
}

double ImuSwingDetector::dynG(const ImuSample &s) const
{
    return qMax(0.0, s.mG - baselineM_);
}

void ImuSwingDetector::accumulateSwingSample(const ImuSample &s, double dyn)
{
    const double accelMag = s.accelMag();
    peakAccelMag_ = qMax(peakAccelMag_, accelMag);

    const double dtSec = lastSwingSampleT_ > 0
                               ? qBound(0.05, (s.tMs - lastSwingSampleT_) / 1000.0, 0.25)
                               : (kImuSampleMs / 1000.0);
    impulseDyn_ += dyn * dtSec;
    lastSwingSampleT_ = s.tMs;

    if (hasOrientRef_) {
        const double orientRate =
            (std::abs(s.rollDeg - lastRollDeg_) + std::abs(s.pitchDeg - lastPitchDeg_)) / dtSec;
        peakGyro_ = qMax(peakGyro_, orientRate);
    }
    lastRollDeg_ = s.rollDeg;
    lastPitchDeg_ = s.pitchDeg;
    hasOrientRef_ = true;
}

double estimateSpeedKmh(const SwingMetrics &m)
{
    const double vAcc = m.peakDynG * kG0 * kTEff;
    const double vGyro = m.peakGyroDps * kGyroScale;
    const double vHandle = 0.55 * vAcc + 0.45 * vGyro;
    const double vPeak = qMax(vHandle * 3.6 * kTipGain, m.peakDynG * kKmPerDynG);

    double vImpulse = 0.0;
    if (m.impulseDynGs > 0.001) {
        vImpulse = m.impulseDynGs * kG0 * kImpulseGain * 3.6;
    }

    double vRate = 0.0;
    if (m.durationMs >= 50 && m.durationMs <= 550 && m.peakDynG > 0.01) {
        const double peakRate = m.peakDynG / (m.durationMs / 1000.0);
        vRate = peakRate * kPeakRateScale * kG0 * 3.6 * 0.42;
    }

    double vAccel3 = 0.0;
    if (m.peakAccelMagG > 1.02) {
        const double dyn3 = m.peakAccelMagG - 1.0;
        vAccel3 = dyn3 * kKmPerDynG * 0.95;
    }

    double speed = vPeak;
    if (vImpulse > 0.0) {
        speed = 0.52 * vPeak + 0.33 * vImpulse + 0.15 * qMax(vRate, vPeak * 0.82);
    } else if (vRate > 0.0) {
        speed = 0.78 * vPeak + 0.22 * vRate;
    }
    if (vAccel3 > 0.0) {
        speed = qMax(speed, 0.88 * vPeak + 0.12 * vAccel3);
    }
    return qBound(8.0, speed, 280.0);
}

int estimatePowerTen(const SwingMetrics &m)
{
    const double dynScore = qMin(1.0, m.peakDynG / 1.15);
    const double gyroScore = qMin(1.0, m.peakGyroDps / 420.0);
    const double mScore = qMin(1.0, qMax(0.0, m.peakMG - 0.98) / 1.35);

    double impulseScore = 0.0;
    if (m.impulseDynGs > 0.001) {
        impulseScore = qMin(1.0, m.impulseDynGs / 0.32);
    }

    double rateScore = 0.0;
    if (m.durationMs >= 50 && m.peakDynG > 0.01) {
        const double peakRate = m.peakDynG / (m.durationMs / 1000.0);
        rateScore = qMin(1.0, peakRate / 2.4);
    }

    double accel3Score = 0.0;
    if (m.peakAccelMagG > 1.0) {
        accel3Score = qMin(1.0, (m.peakAccelMagG - 1.0) / 1.1);
    }

    double blend = 0.0;
    if (m.impulseDynGs > 0.001) {
        blend = 0.28 * dynScore + 0.22 * gyroScore + 0.10 * mScore + 0.22 * impulseScore + 0.12 * rateScore
                + 0.06 * accel3Score;
    } else {
        blend = 0.36 * dynScore + 0.26 * gyroScore + 0.12 * mScore + 0.14 * rateScore + 0.12 * accel3Score;
    }
    return qBound(1, static_cast<int>(qRound(1.0 + blend * 9.0)), 10);
}

namespace {

constexpr int kStrokeClassCount = 6;

struct StrokeScoreProfile {
    int idealPowerTen;
    int minPowerTen;
    int maxPowerTen;
    float cnnWeight;
    float powerWeight;
};

/* 理想力度参照羽毛球生物力学：杀球>高远>平抽/发球>挑球>放网（Tsai 1997 等） */
static const StrokeScoreProfile kStrokeProfiles[kStrokeClassCount] = {
    {7, 5, 9, 0.52f, 0.48f},  /* 高远 ~48 m/s，完整挥拍中等偏高 */
    {6, 4, 8, 0.55f, 0.45f},  /* 平抽 中场快速平击 */
    {5, 3, 7, 0.60f, 0.40f},  /* 挑球 轻柔上挑 */
    {3, 1, 5, 0.65f, 0.35f},  /* 放网 ~25 m/s，控力为主 */
    {5, 3, 7, 0.58f, 0.42f},  /* 发球 稳定可控 */
    {9, 7, 10, 0.48f, 0.52f}, /* 杀球 ~62–68 m/s，爆发最高 */
};

static const StrokeScoreProfile kDefaultStrokeProfile = {5, 2, 8, 0.58f, 0.42f};

static int strokeClassFromLabel(const QString &hitType)
{
    const QString t = hitType.trimmed();
    if (t.isEmpty() || t == QStringLiteral("挥拍"))
        return -1;
    if (t.contains(QStringLiteral("杀")))
        return 5;
    if (t.contains(QStringLiteral("高远")))
        return 0;
    if (t.contains(QStringLiteral("平抽")))
        return 1;
    if (t.contains(QStringLiteral("挑")))
        return 2;
    if (t.contains(QStringLiteral("放网")) || t.contains(QStringLiteral("搓"))
        || t.contains(QStringLiteral("网")))
        return 3;
    if (t.contains(QStringLiteral("发")))
        return 4;
    return -1;
}

static const StrokeScoreProfile &strokeScoreProfileFor(int strokeClassId, const QString &hitTypeLabel)
{
    if (strokeClassId >= 0 && strokeClassId < kStrokeClassCount)
        return kStrokeProfiles[strokeClassId];
    const int fromLabel = strokeClassFromLabel(hitTypeLabel);
    if (fromLabel >= 0 && fromLabel < kStrokeClassCount)
        return kStrokeProfiles[fromLabel];
    return kDefaultStrokeProfile;
}

static double mapCnnMatchScore(float strokeConfidence)
{
    if (strokeConfidence <= 0.01f)
        return 0.38;
    const double c = qBound(0.0, static_cast<double>(strokeConfidence), 1.0);
    constexpr double lo = 0.15;
    constexpr double hi = 0.88;
    return qBound(0.0, (c - lo) / (hi - lo), 1.0);
}

static double strokePowerMatchScore(int powerTen, const StrokeScoreProfile &p)
{
    const int power = qBound(1, powerTen, 10);
    const double ideal = p.idealPowerTen;
    const double tol = qMax(1.5, (p.maxPowerTen - p.minPowerTen) * 0.55 + 0.5);
    const double diff = std::abs(power - ideal);
    double fit = qMax(0.0, 1.0 - diff / tol);

    if (power < p.minPowerTen) {
        fit *= qMax(0.15, static_cast<double>(power) / qMax(1, p.minPowerTen));
    }
    if (power > p.maxPowerTen && p.idealPowerTen <= 4) {
        fit *= qMax(0.25, 1.0 - (power - p.maxPowerTen) * 0.18);
    } else if (power > p.maxPowerTen && p.idealPowerTen >= 8) {
        fit = qMin(1.0, fit + 0.08);
    }
    return qBound(0.0, fit, 1.0);
}

} // namespace

int classHitScoreFromImu(int strokeClassId, const QString &hitTypeLabel, float strokeConfidence, int powerTen)
{
    constexpr int kBase = 50;
    constexpr int kBonusMax = 49;

    const StrokeScoreProfile &prof = strokeScoreProfileFor(strokeClassId, hitTypeLabel);
    const double cnnPart = mapCnnMatchScore(strokeConfidence);
    const double powerPart = strokePowerMatchScore(powerTen, prof);
    const double wSum = qMax(0.01f, prof.cnnWeight + prof.powerWeight);
    const double blend = (cnnPart * prof.cnnWeight + powerPart * prof.powerWeight) / wSum;
    return qBound(kBase, kBase + static_cast<int>(qRound(kBonusMax * blend)), 99);
}

int estimatePowerTen(double peakDynG, double peakGyroDps, double peakMG)
{
    SwingMetrics m;
    m.peakDynG = peakDynG;
    m.peakGyroDps = peakGyroDps;
    m.peakMG = peakMG;
    return estimatePowerTen(m);
}

std::optional<SwingEstimate> ImuSwingDetector::finish(int tEnd)
{
    const double minDyn = ruleMinPeakDynG();
    const double minGyro = ruleMinPeakGyroDps();
    /* 已进入 swing：过滤弱峰（需 dyn 或 gyro 至少一项达标） */
    if (peakDyn_ < minDyn * 0.55 && peakGyro_ < minGyro * 0.55) {
        armed_ = false;
        onConfirm_ = 0;
        return std::nullopt;
    }
    const int dur = qMax(1, tEnd - swingT0_);
    const double peakDyn = peakDyn_;
    const double peakGyro = peakGyro_;
    const double peakM = peakM_;

    SwingMetrics metrics;
    metrics.peakDynG = peakDyn;
    metrics.peakGyroDps = peakGyro;
    metrics.peakMG = peakM;
    metrics.durationMs = dur;
    metrics.impulseDynGs = impulseDyn_;
    metrics.peakAccelMagG = peakAccelMag_;

    SwingEstimate est;
    est.speedKmh = estimateSpeedKmh(metrics);
    est.peakDynG = peakDyn;
    est.peakGyro = peakGyro;
    est.peakMG = peakM;
    est.peakAccelMagG = peakAccelMag_;
    est.impulseDynGs = impulseDyn_;
    est.powerTen = estimatePowerTen(metrics);
    est.durationMs = dur;
    est.swingT0Ms = swingT0_;
    est.swingTEndMs = tEnd;
    return est;
}

std::optional<SwingEstimate> ImuSwingDetector::feed(const ImuSample &s)
{
    const double dyn = dynG(s);
    const double mStep = (prevSampleT_ > 0 && prevMG_ > 0.01) ? qMax(0.0, s.mG - prevMG_) : 0.0;
    const double gyro = effectiveGyroDps(s);

    auto trackSample = [&]() {
        prevMG_ = s.mG;
        prevRollTrack_ = s.rollDeg;
        prevPitchTrack_ = s.pitchDeg;
        prevSampleT_ = s.tMs;
    };

    auto tryStartSwing = [&]() -> bool {
        if (!swingTrigger(dyn, gyro, mStep)) {
            onConfirm_ = 0;
            return false;
        }
        onConfirm_++;
        if (onConfirm_ < ruleConfirmSamples())
            return false;
        onConfirm_ = 0;
        state_ = "swing";
        swingT0_ = s.tMs;
        peakDyn_ = dyn;
        peakGyro_ = gyro;
        peakM_ = s.mG;
        peakAccelMag_ = s.accelMag();
        impulseDyn_ = 0.0;
        lastSwingSampleT_ = 0;
        lastRollDeg_ = s.rollDeg;
        lastPitchDeg_ = s.pitchDeg;
        hasOrientRef_ = false;
        armed_ = false;
        accumulateSwingSample(s, dyn);
        return true;
    };

    if (dyn < kSwingOffG)
        armed_ = true;

    if (state_ == QLatin1String("idle")) {
        updateBaseline(s, dyn);
        tryStartSwing();
        trackSample();
        return std::nullopt;
    }

    if (state_ == QLatin1String("swing")) {
        peakDyn_ = qMax(peakDyn_, dyn);
        peakGyro_ = qMax(peakGyro_, gyro);
        peakM_ = qMax(peakM_, s.mG);
        accumulateSwingSample(s, dyn);

        int elapsed = s.tMs - swingT0_;
        if (elapsed < 0)
            elapsed = 0;
        const bool off = dyn < kSwingOffG && elapsed >= kSwingMinMs;
        const bool timeout = elapsed >= kSwingMaxMs;
        if (off || timeout) {
            baselineM_ = 0.75 * baselineM_ + 0.25 * s.mG;
            std::optional<SwingEstimate> est = finish(s.tMs);
            state_ = "cooldown";
            lastFinishT_ = s.tMs;
            armed_ = false;
            onConfirm_ = 0;
            trackSample();
            return est;
        }
        trackSample();
        return std::nullopt;
    }

    if (state_ == QLatin1String("cooldown")) {
        updateBaseline(s, dyn);
        int sinceFinish = s.tMs - lastFinishT_;
        if (sinceFinish < 0)
            sinceFinish = ruleCooldownMs() + 1;
        if (sinceFinish > ruleCooldownMs()) {
            state_ = "idle";
            armed_ = true;
            peakDyn_ = 0;
            peakGyro_ = 0;
            peakM_ = 0;
            peakAccelMag_ = 0;
            impulseDyn_ = 0;
            lastSwingSampleT_ = 0;
            hasOrientRef_ = false;
            onConfirm_ = 0;
        }
        trackSample();
        return std::nullopt;
    }

    state_ = "idle";
    armed_ = true;
    trackSample();
    return std::nullopt;
}
