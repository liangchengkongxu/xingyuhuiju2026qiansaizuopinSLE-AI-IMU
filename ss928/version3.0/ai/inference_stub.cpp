#include "inference_stub.h"

#include <QString>

AiInferenceEngine::AiInferenceEngine(const AiInferenceConfig &cfg)
    : m_cfg(cfg)
{
    if (m_cfg.modelPath.isEmpty())
        m_cfg.modelPath = QStringLiteral("/opt/widget_ui/models");
}

bool AiInferenceEngine::loadModel(const QString &modelPath)
{
    Q_UNUSED(modelPath);
    // TODO: 接入 SVP NPU / ACL，加载 .om 等
    m_loaded = false;
    return false;
}

void AiInferenceEngine::unloadModel()
{
    m_loaded = false;
}

bool AiInferenceEngine::isLoaded() const
{
    return m_loaded;
}

AiInferenceResult AiInferenceEngine::inferFromFrame(const void *data, int width, int height, int stride)
{
    Q_UNUSED(data);
    Q_UNUSED(width);
    Q_UNUSED(height);
    Q_UNUSED(stride);

    AiInferenceResult r;
    r.ok = false;
    r.message = QStringLiteral("AI inference not implemented yet (version3.0 stub)");
    return r;
}
