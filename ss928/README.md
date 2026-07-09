# SS928 主控端（端侧智能 + 星闪 IMU）

海思 **SS928V100** 上的 Qt 训练面板：星闪扫描拍柄广播、IMU 挥拍检测、1D CNN 动作分类、YOLO 视觉辅助。

完整工程依赖 MPP SDK，体积较大，本仓库提供：

- **对接文档**（`docs/`）
- **与量产对齐的参考代码**（`reference/ws73/`）

---

## 能力概览

| 功能 | 说明 |
|------|------|
| 星闪 IMU 接收 | `sle_seek_print_all` 扫描 + `sle_imu_bridge.sh` 写 `/tmp/sle_imu_lines` |
| ASCII 解析 + 去重 | `sle_seek_print_client.c`：0xFF → `@` 行，MAC+uptime 去重 |
| 挥拍检测 | `imu_swing_detector.cpp` 规则 FSM |
| 动作分类 | `imu_cnn_classifier.cpp` + `badminton_npu.om` |
| 训练 UI | 单人练习 / 班级 / 对打等多页面 |

---

## 目录结构

```
ss928/
├── README.md                    # 本文件
├── docs/
│   ├── 主控待办清单.md           # 对接 checklist（含完成状态）
│   ├── 部署与运行.md
│   ├── IMU解析与去重实现.md
│   └── 关键源码索引.md           # 完整工程内文件路径
└── reference/ws73/              # 可从本仓直接阅读的参考实现
    ├── sle_imu_bridge.sh
    └── sle_seek_print_all/
        ├── sle_seek_print_client.c
        └── sle_imu_adv.h
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

## 状态说明

- **协议与解析**：已与 2026-07 拍柄 ASCII 固件对齐（见 `reference/`）
- **完整 Qt 工程**：队伍内部 SS928 SDK 工作区维护，按索引文档定位源码
