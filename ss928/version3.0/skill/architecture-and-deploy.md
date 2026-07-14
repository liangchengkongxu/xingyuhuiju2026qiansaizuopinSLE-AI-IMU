# 架构与部署（摘要）

完整版见 [`../README.md`](../README.md)。本文仅保留 Agent 必备信息。

---

## 默认 attach 数据流

```
VI(OS08A20) → VPSS grp0
  ├─ ch0  3840×2160  预览 + Region 人体框
  ├─ ch1   224×224   → best_aipp_fix.om（5~6 类动作）
  └─ ch2   640×640   → /opt/yolov8n.om（人体/动作框）

vo_gfbg_init → camera_pipe → fork sample_vio_ai attach → GFBG → fork widget_panel
```

Qt 读 `/tmp/.widget_yolo_action`；AI 日志 `/tmp/sample_vio_ai.log`。

---

## PC 交叉编译

```bash
cd version3.0
bash scripts/build_vio_ai.sh      # → bin/sample_vio_ai
bash scripts/build_vo_gfbg_init.sh # → bin/vo_gfbg_init
bash scripts/build_imu_cnn.sh     # → lib/libimu_cnn.a（可选）
```

工具链：`aarch64-mix210-linux-gcc`，PATH 含 SDK 交叉编译 bin。

---

## 一键 / 分步部署

```bash
cd version3.0
bash scripts/import_deploy_pack_cls.sh   # 首次：分类模型 → models/
bash scripts/deploy_models.sh            # models/ → 板端
bash scripts/deploy.sh                   # 源码 + bin + 板端 make + run.sh
```

跳过模型：`WIDGET_DEPLOY_SKIP_MODELS=1 bash scripts/deploy.sh`  
只部署不启动：`WIDGET_DEPLOY_SKIP_RUN=1 bash scripts/deploy.sh`

---

## 板端目录

| 路径 | 说明 |
|------|------|
| `/opt/widget_ui/widget_panel` | Qt 主程序 |
| `/opt/widget_ui/vo_gfbg_init` | 启动器 |
| `/opt/widget_ui/bin/sample_vio_ai` | NPU attach |
| `/opt/widget_ui/models/` | om + label_map |
| `/opt/yolov8n.om` | 检测模型 |
| `/opt/widget_ui/ws73/` | 星闪脚本与 ko |
| `/opt/widget_ui/tutorials/*.mp4` | 教学视频 |

---

## AICPU / ACL（IMU CNN 必需）

`deploy.sh` 会建软链：

```
/usr/lib64/aicpu_kernels/*.so → /opt/lib/npu/
```

`run.sh`：`ASCEND_AICPU_KERNEL_PATH=/opt/lib/npu`，`LD_LIBRARY_PATH` 含 `/opt/lib/npu`。

---

## 验证清单

```bash
ps | grep -E 'widget_panel|vo_gfbg_init|sample_vio_ai'
grep cls /tmp/sample_vio_ai.log | tail -5
cat /tmp/.widget_yolo_action
file /opt/widget_ui/widget_panel   # ARM aarch64, 非 empty
```
