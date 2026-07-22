# sample_vio_ai 模块结构

`sample_vio_ai` 由单文件拆分为多模块，源码在 `version3.0/ai/`，构建时由 `scripts/build_vio_ai.sh` 复制到 `smp/a55_linux/mpp/sample/vio_ai/` 并链接。

## 文件说明

| 文件 | 行数（约） | 职责 |
|------|-----------|------|
| `sample_vio_ai.c` | ~5670 | 主流程：VIO 管道、YOLO 检测/分类、RGN 叠加、ACL 推理入口 |
| `vio_ai_internal.h` | ~155 | 共享类型、`pose_result_t`/`yolo_det_t`、跨模块 API 与全局变量声明 |
| `vio_ai_env.c` | ~20 | 环境变量读取 `WIDGET_*` |
| `vio_ai_yuv.c` | ~310 | NV12/NV21 缩放、letterbox、Y 平面画线/画框 |
| `vio_ai_pose.c` | ~920 | Pose 推理、RGN 骨骼、回放 stamp、后处理 |
| `vio_ai_hit_replay.c` | ~1210 | 击球回放环形缓冲、按需骨骼渲染、导出 PPM/raw |

## 编译

```bash
bash version3.0/scripts/build_vio_ai.sh
# 输出: version3.0/bin/sample_vio_ai
```

Makefile 使用 `$(wildcard $(PWD)/*.c)`，新增 `vio_ai_*.c` 会自动参与编译。

## 其他

- `inference_stub.h` / `inference_stub.cpp`：Qt 侧推理占位，与板端 `sample_vio_ai` 独立。
- SDK 参考：`smp/a55_linux/mpp/sample/svp/svp_npu/`、`sample/svp/common/sample_common_svp.c`
