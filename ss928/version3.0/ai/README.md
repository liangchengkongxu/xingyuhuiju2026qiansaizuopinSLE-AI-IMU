# AI 推理模块（待实现）

本目录存放 **version3.0** 的模型推理封装，与 Qt 面板（`src/main.cpp`）及摄像头管道（`src/camera_pipe.*`）解耦。

## 规划接口

见 `inference_stub.h`：后续替换为真实 SVP NPU / ACL 实现。

## 集成步骤（待你提供模型细节后完成）

1. 在 `ai/` 实现 `AiInferenceEngine`（加载 `models/`、跑推理、输出结构化结果）
2. 从 VPSS/VI 或现有预览路径取帧，做 resize / 归一化
3. 在训练页或专用 AI 页订阅推理结果，更新 UI
4. `src/Makefile` 增加 `ai/*.cpp` 与 NPU 库链接

## SDK 参考

- `smp/a55_linux/mpp/sample/svp/svp_npu/`
- `smp/a55_linux/mpp/sample/svp/common/sample_common_svp.c`
