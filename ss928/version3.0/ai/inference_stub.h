#ifndef AI_INFERENCE_STUB_H
#define AI_INFERENCE_STUB_H

#include <QString>

// version3.0 占位：待接入真实 NPU 推理后替换本头文件与实现。

struct AiInferenceConfig {
    QString modelPath;   // 默认 /opt/widget_ui/models/
    int inputWidth = 0;
    int inputHeight = 0;
};

struct AiInferenceResult {
    bool ok = false;
    QString message;
    // 后续扩展：检测框、动作类别、置信度等
};

class AiInferenceEngine {
public:
    explicit AiInferenceEngine(const AiInferenceConfig &cfg = AiInferenceConfig());

    bool loadModel(const QString &modelPath);
    void unloadModel();
    bool isLoaded() const;

    // 输入：RGB/YUV 帧缓冲；输出：AiInferenceResult（当前为占位）
    AiInferenceResult inferFromFrame(const void *data, int width, int height, int stride);

private:
    AiInferenceConfig m_cfg;
    bool m_loaded = false;
};

#endif
