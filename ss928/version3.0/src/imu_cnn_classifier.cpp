#include "imu_cnn_classifier.h"

#include "badminton_npu_infer.h"

#include <QFileInfo>
#include <algorithm>
#include <cmath>

namespace {

constexpr int kRingCap = 64;
constexpr int kWindow = BADMINTON_NPU_WINDOW_SIZE;

float classMinConf()
{
    bool ok = false;
    const int pct = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_CLASS_CONF", &ok);
    if (ok && pct > 0 && pct <= 100) {
        return static_cast<float>(pct) / 100.0f;
    }
    const QByteArray raw = qgetenv("WIDGET_IMU_CNN_CLASS_CONF");
    if (!raw.isEmpty()) {
        bool fok = false;
        const float v = raw.toFloat(&fok);
        if (fok && v > 0.01f && v <= 1.0f) {
            return v;
        }
    }
    return 0.15f;
}

float triggerMinConf()
{
    bool ok = false;
    const int pct = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_TRIGGER_CONF", &ok);
    if (ok && pct > 0 && pct <= 100) {
        return static_cast<float>(pct) / 100.0f;
    }
    const QByteArray raw = qgetenv("WIDGET_IMU_CNN_TRIGGER_CONF");
    if (!raw.isEmpty()) {
        bool fok = false;
        const float v = raw.toFloat(&fok);
        if (fok && v > 0.01f && v <= 1.0f) {
            return v;
        }
    }
    return 0.22f;
}

float triggerSoftConf()
{
    bool ok = false;
    const int pct = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_SOFT_CONF", &ok);
    if (ok && pct > 0 && pct <= 100) {
        return static_cast<float>(pct) / 100.0f;
    }
    const QByteArray raw = qgetenv("WIDGET_IMU_CNN_SOFT_CONF");
    if (!raw.isEmpty()) {
        bool fok = false;
        const float v = raw.toFloat(&fok);
        if (fok && v > 0.01f && v <= 1.0f) {
            return v;
        }
    }
    return 0.20f;
}

int triggerCooldownMs()
{
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_COOLDOWN_MS", &ok);
    if (ok && ms >= 200) {
        return ms;
    }
    return 500;
}

float triggerMinDynG()
{
    bool ok = false;
    const int centiG = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_TRIGGER_MIN_DYN", &ok);
    if (ok && centiG >= 0 && centiG <= 50) {
        if (centiG == 0) {
            return 0.02f;
        }
        return static_cast<float>(centiG) / 100.0f;
    }
    return 0.06f;
}

float triggerSoftMinDynG()
{
    bool ok = false;
    const int centiG = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_SOFT_MIN_DYN", &ok);
    if (ok && centiG >= 0 && centiG <= 20) {
        if (centiG == 0) {
            return 0.015f;
        }
        return static_cast<float>(centiG) / 100.0f;
    }
    return 0.04f;
}

float triggerSoftMinMG()
{
    bool ok = false;
    const int mgX100 = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_SOFT_MIN_M", &ok);
    if (ok && mgX100 >= 40 && mgX100 <= 120) {
        return static_cast<float>(mgX100) / 100.0f;
    }
    return 0.44f;
}

float triggerSoftMinGyroDps()
{
    bool ok = false;
    const int dps = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_SOFT_MIN_GYRO", &ok);
    if (ok && dps >= 15 && dps <= 120) {
        return static_cast<float>(dps);
    }
    return 38.0f;
}

float triggerMinMG()
{
    bool ok = false;
    const int mgX100 = qEnvironmentVariableIntValue("WIDGET_IMU_CNN_TRIGGER_MIN_M", &ok);
    if (ok && mgX100 >= 40) {
        return static_cast<float>(mgX100) / 100.0f;
    }
    return 0.46f;
}

void fillSoftmaxProbs(const badminton_npu_result_t &npu, float probs[BADMINTON_NPU_NUM_CLASSES])
{
    float maxLogit = npu.logits[0];
    for (int i = 1; i < BADMINTON_NPU_NUM_CLASSES; ++i) {
        maxLogit = std::max(maxLogit, npu.logits[i]);
    }
    float sum = 0.0f;
    for (int i = 0; i < BADMINTON_NPU_NUM_CLASSES; ++i) {
        probs[i] = std::exp(npu.logits[i] - maxLogit);
        sum += probs[i];
    }
    if (sum > 0.0f) {
        for (int i = 0; i < BADMINTON_NPU_NUM_CLASSES; ++i) {
            probs[i] /= sum;
        }
    }
}

} // namespace

ImuCnnClassifier::ImuCnnClassifier() = default;

ImuCnnClassifier::~ImuCnnClassifier()
{
    if (ready_) {
        badminton_npu_deinit();
        ready_ = false;
    }
}

bool ImuCnnClassifier::init(const QString &omPath)
{
    const QString path = omPath.trimmed();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return false;
    }
    if (ready_) {
        return true;
    }
    if (badminton_npu_init(path.toUtf8().constData()) != 0) {
        return false;
    }
    ready_ = true;
    return true;
}

void ImuCnnClassifier::resetAll()
{
    states_.clear();
}

void ImuCnnClassifier::resetMac(const QString &mac)
{
    states_.remove(normMac(mac));
}

QString ImuCnnClassifier::normMac(const QString &mac)
{
    QString s = mac.trimmed().toUpper();
    s.replace(QLatin1Char('-'), QLatin1Char(':'));
    if (s.isEmpty()) {
        return QStringLiteral("_unknown_");
    }
    return s;
}

void ImuCnnClassifier::pushSample(const QString &mac, const ImuSample &sample)
{
    RingEntry e;
    e.tMs = sample.tMs;
    e.mG = static_cast<float>(sample.mG);
    e.gyroDps = static_cast<float>(sample.gyroMag());
    for (int i = 0; i < 8; ++i) {
        e.ch[i] = sample.rawCh[i];
    }

    MacState &st = states_[normMac(mac)];
    st.ring.append(e);
    while (st.ring.size() > kRingCap) {
        st.ring.removeFirst();
    }
}

bool ImuCnnClassifier::extractWindow(const MacState &st, int centerIdx, float out[8][24]) const
{
    if (st.ring.isEmpty()) {
        return false;
    }
    centerIdx = std::clamp(centerIdx, 0, st.ring.size() - 1);
    const int half = kWindow / 2;
    int start = centerIdx - half;
    if (start < 0) {
        start = 0;
    }
    if (start + kWindow > st.ring.size()) {
        start = std::max(0, st.ring.size() - kWindow);
    }

    for (int t = 0; t < kWindow; ++t) {
        int idx = start + t;
        if (idx < 0 || idx >= st.ring.size()) {
            for (int c = 0; c < 8; ++c) {
                out[c][t] = 0.0f;
            }
            continue;
        }
        const RingEntry &e = st.ring.at(idx);
        for (int c = 0; c < 8; ++c) {
            out[c][t] = e.ch[c];
        }
    }
    return true;
}

std::optional<ImuStrokeResult> ImuCnnClassifier::classifySwing(const QString &mac,
    int swingT0Ms, int swingTEndMs, float minConfOverride)
{
    if (!ready_) {
        return std::nullopt;
    }

    const QString key = normMac(mac);
    if (!states_.contains(key) || states_.value(key).ring.isEmpty()) {
        return std::nullopt;
    }
    const MacState &st = states_[key];

    int peakIdx = st.ring.size() - 1;
    float bestM = -1.0f;
    for (int i = 0; i < st.ring.size(); ++i) {
        const RingEntry &e = st.ring.at(i);
        if (swingT0Ms > 0 && e.tMs < swingT0Ms) {
            continue;
        }
        if (swingTEndMs > 0 && e.tMs > swingTEndMs + kWindow * 100) {
            continue;
        }
        if (e.mG > bestM) {
            bestM = e.mG;
            peakIdx = i;
        }
    }

    const float minConf = (minConfOverride >= 0.0f) ? minConfOverride : classMinConf();
    badminton_npu_result_t bestNpu = {};
    float bestConf = -1.0f;
    bool hasBest = false;

    for (int offset = -1; offset <= 1; ++offset) {
        const int center = peakIdx + offset;
        if (center < 0 || center >= st.ring.size()) {
            continue;
        }
        float window[8][24];
        if (!extractWindow(st, center, window)) {
            continue;
        }
        badminton_npu_result_t npu;
        if (badminton_npu_infer(window, &npu) != 0) {
            continue;
        }
        float probs[BADMINTON_NPU_NUM_CLASSES];
        fillSoftmaxProbs(npu, probs);
        const float conf = probs[npu.class_id];
        if (!hasBest || conf > bestConf) {
            hasBest = true;
            bestConf = conf;
            bestNpu = npu;
            bestNpu.class_id = npu.class_id;
            bestNpu.confidence = conf;
        }
    }

    if (!hasBest || bestConf < minConf) {
        return std::nullopt;
    }

    ImuStrokeResult out;
    out.classId = bestNpu.class_id;
    out.confidence = bestConf;
    out.labelCn = QString::fromUtf8(badminton_npu_label_cn(bestNpu.class_id));
    return out;
}

bool ImuCnnClassifier::hadRecentHit(const QString &mac, int tMs, int cooldownMs) const
{
    if (cooldownMs <= 0 || tMs <= 0) {
        return false;
    }
    const QString key = normMac(mac);
    if (!states_.contains(key)) {
        return false;
    }
    const MacState &st = states_.value(key);
    return st.lastHitTMs > 0 && tMs <= st.lastHitTMs + cooldownMs;
}

std::optional<ImuCnnHitEvent> ImuCnnClassifier::tryDetectHit(const QString &mac)
{
    if (!ready_) {
        return std::nullopt;
    }

    const QString key = normMac(mac);
    MacState &st = states_[key];
    const int n = st.ring.size();
    if (n < kWindow + 1) {
        return std::nullopt;
    }

    const RingEntry &latest = st.ring.at(n - 1);
    if (!st.armed && latest.mG < 0.42f) {
        st.armed = true;
    }
    if (!st.armed) {
        return std::nullopt;
    }

    const int peakIdx = n - 2;
    const RingEntry &prev = st.ring.at(peakIdx - 1);
    const RingEntry &peak = st.ring.at(peakIdx);
    const RingEntry &next = st.ring.at(peakIdx + 1);
    const float dynPeak = peak.mG - 1.0f;
    const float dynPrev = prev.mG - 1.0f;

    bool sharpPeak = peak.mG >= prev.mG && peak.mG >= next.mG;
    if (sharpPeak) {
        for (int j = peakIdx - 2; j <= peakIdx + 2; ++j) {
            if (j < 0 || j >= n || j == peakIdx) {
                continue;
            }
            if (st.ring.at(j).mG > peak.mG + 0.005f) {
                sharpPeak = false;
                break;
            }
        }
    }

    const bool sharpOk = sharpPeak && peak.mG >= triggerMinMG() && dynPeak >= triggerMinDynG();
    const float softMinMG = triggerSoftMinMG();
    const float softMinDyn = triggerSoftMinDynG();
    const float softMinGyro = triggerSoftMinGyroDps();
    const bool softOk = (peak.mG >= softMinMG && dynPeak >= softMinDyn)
        || (peak.gyroDps >= softMinGyro && dynPeak >= softMinDyn * 0.75f)
        || (prev.mG >= softMinMG && dynPrev >= softMinDyn && peak.mG >= softMinMG - 0.008f);

    if (!sharpOk && !softOk) {
        return std::nullopt;
    }

    const float minConf = sharpOk ? triggerMinConf() : triggerSoftConf();
    const int cooldownMs = triggerCooldownMs();
    if (peak.tMs <= st.lastHitTMs + cooldownMs) {
        return std::nullopt;
    }

    if (peak.tMs == st.lastProcessedPeakTMs) {
        return std::nullopt;
    }
    st.lastProcessedPeakTMs = peak.tMs;

    badminton_npu_result_t bestNpu = {};
    float bestProbs[BADMINTON_NPU_NUM_CLASSES] = {0};
    bool hasBest = false;
    int bestCenter = peakIdx;
    float bestConf = -1.0f;

    for (int offset = -1; offset <= 1; ++offset) {
        const int center = peakIdx + offset;
        if (center < 0 || center >= n) {
            continue;
        }
        float window[8][24];
        if (!extractWindow(st, center, window)) {
            continue;
        }
        badminton_npu_result_t npu;
        if (badminton_npu_infer(window, &npu) != 0) {
            continue;
        }
        float localProbs[BADMINTON_NPU_NUM_CLASSES];
        fillSoftmaxProbs(npu, localProbs);
        const float conf = localProbs[npu.class_id];
        if (!hasBest || conf > bestConf) {
            hasBest = true;
            bestConf = conf;
            bestNpu = npu;
            bestCenter = center;
            bestNpu.class_id = npu.class_id;
            bestNpu.confidence = conf;
            for (int i = 0; i < BADMINTON_NPU_NUM_CLASSES; ++i) {
                bestProbs[i] = localProbs[i];
            }
        }
    }

    if (!hasBest) {
        return std::nullopt;
    }

    if (bestConf < minConf) {
        return std::nullopt;
    }

    st.lastHitTMs = peak.tMs;
    st.armed = false;

    ImuCnnHitEvent ev;
    ev.stroke.classId = bestNpu.class_id;
    ev.stroke.confidence = bestConf;
    ev.stroke.labelCn = QString::fromUtf8(badminton_npu_label_cn(bestNpu.class_id));
    ev.peakTMs = peak.tMs;
    ev.peakMG = peak.mG;
    ev.peakGyro = peak.gyroDps;
    const int halfMs = (kWindow / 2) * 100;
    const int centerTMs = st.ring.at(bestCenter).tMs;
    ev.swingT0Ms = centerTMs - halfMs;
    ev.swingTEndMs = centerTMs + halfMs;
    for (int i = 0; i < BADMINTON_NPU_NUM_CLASSES; ++i) {
        ev.probs[i] = bestProbs[i];
    }
    return ev;
}
