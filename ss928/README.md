# SS928 主控端（端侧智能 + 星闪 IMU）

海思 **SS928V100** 上的 Qt 训练面板：星闪扫描拍柄广播、IMU 挥拍检测、1D CNN 动作分类、YOLO 视觉辅助。

完整工程依赖 MPP SDK，体积较大。本仓库提供：

- **完整 Qt 工程源码**（`version3.0/`，不含 `.om` 等大模型与板端二进制）
- **对接文档**（`docs/`）
- **与量产对齐的参考代码**（`reference/ws73/`）
- **Agent 技能文档**（`version3.0/skill/`，Pose/回放/部署踩坑）

---

## 能力概览

| 功能 | 说明 |
|------|------|
| 星闪 IMU 接收 | `sle_seek_print_all` 扫描 + `sle_imu_bridge.sh` 写 `/tmp/sle_imu_lines` |
| ASCII 解析 + 去重 | `sle_seek_print_client.c`：0xFF → `@` 行，MAC+uptime 去重 |
| 挥拍检测 | `imu_swing_detector.cpp` 规则 FSM |
| 动作分类 | `imu_cnn_classifier.cpp` + `badminton_npu.om` |
| 训练 UI | 单人练习 / 班级 / 对打等多页面 |
| Pose 骨骼 RGN | `sample_vio_ai.c` + VPSS ch2 640×640 NPU 推理，RGN 叠加 4K 预览 |
| 击球回放 | ch1 640 源 → 960×540 PPM 序列 + 软件画骨骼 |

---

## 目录结构

```
ss928/
├── README.md                    # 本文件
├── version3.0/                  # ★ 完整 Qt + AI 工程（源码，无大模型）
│   ├── src/                     # widget_panel、训练 UI、IMU 服务
│   ├── ai/                      # sample_vio_ai（YOLO + Pose + 回放）
│   ├── ws73/                    # 星闪扫描工具
│   ├── scripts/                 # build/deploy/run.sh
│   └── skill/                   # Agent 文档（Pose、回放、部署）
├── docs/
│   ├── 主控待办清单.md
│   ├── 部署与运行.md
│   ├── IMU解析与去重实现.md
│   └── 关键源码索引.md
└── reference/ws73/              # sle_seek_print_client.c 参考快照
```

---

## 快速验证（板端）

```bash
/opt/widget_ui/ws73/sle_imu_bridge.sh start
tail -f /tmp/sle_imu_lines
# 期望：约 10Hz，uptime 每 ~100ms 递增，同 uptime 不重复刷屏
```

---

## 与拍柄协议

| 项目 | 值 |
|------|-----|
| 设备名 | `paibing_imu` |
| MAC | `cc:ad:c9:00:22:01` … `06` |
| 载荷 | ADV / Scan Response 的 `0xFF` 厂商域，**ASCII 行** |
| 格式 | `@ms,A...,G...,R...,P...,M...\n` |

详见 [`../bs20/docs/主控对接说明.md`](../bs20/docs/主控对接说明.md)。

---

## 文档导航

| 文档 | 用途 |
|------|------|
| [主控待办清单](docs/主控待办清单.md) | 对接必做项 |
| [IMU解析与去重实现](docs/IMU解析与去重实现.md) | 解析顺序、去重逻辑 |
| [部署与运行](docs/部署与运行.md) | 编译部署、环境变量 |
| [关键源码索引](docs/关键源码索引.md) | 完整工程文件地图 |

---

## 本地编译与部署（开发机）

```bash
# 在 SS928 MPP SDK 工作区根目录，或 clone 本仓后按 version3.0/README.md 配置 SDK 路径
bash ss928/version3.0/scripts/build_vio_ai.sh
bash ss928/version3.0/scripts/deploy_bin.sh          # sample_vio_ai + vo_gfbg_init
bash ss928/version3.0/scripts/deploy_panel.sh        # widget_panel 增量
bash ss928/version3.0/scripts/deploy_ws73.sh           # 星闪工具
```

Pose / 回放关键 env（`run.sh`）：`WIDGET_REPLAY_VPSS_CHN=-1`（**禁止 ch0 抢帧**）、`WIDGET_POSE_INTERVAL=2`。详见 [`version3.0/skill/hit-replay-and-pose-rgn.md`](version3.0/skill/hit-replay-and-pose-rgn.md)。

## 状态说明

- **协议与解析**：已与 2026-07 拍柄 22B 二进制固件对齐（见 `reference/`）
- **完整 Qt 工程**：本仓 `version3.0/` 与队伍 SDK 工作区同步维护
