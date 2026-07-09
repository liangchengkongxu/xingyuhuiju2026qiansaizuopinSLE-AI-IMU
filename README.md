# 星羽汇聚 · 2026 嵌赛 · 星闪 IMU 智能羽毛球训练系统

基于 **海思 SS928 端侧智能** + **BS20 星闪拍柄** 的智能羽毛球训练方案：拍柄九轴无连接广播，主控星闪扫描、挥拍检测与 1D CNN 动作分类。

| 模块 | 目录 | 角色 |
|------|------|------|
| **BS20 拍柄** | [`bs20/`](bs20/) | MPU-9250 采集、Mahony 姿态、星闪/BLE 上报 |
| **SS928 主控** | [`ss928/`](ss928/) | 星闪扫描解析、Qt 训练 UI、端侧 AI |
| **项目文档** | [`docs/`](docs/) | 联调指南、开发历程、目录说明 |
| **Agent Skill** | [`.cursor/skills/`](.cursor/skills/) | Cursor 二次开发参考（拍柄/主控/全栈） |

**GitHub**：https://github.com/liangchengkongxu/xingyuhuiju2026qiansaizuopinSLE-AI-IMU

---

## 系统架构（2026-07 当前协议）

```
  ┌──────────────────┐   星闪非连接扫播 (5ms 间隔)    ┌─────────────────────────┐
  │  BS20 拍柄        │  ADV + Scan Response 双份      │  SS928 主控 (WS73)       │
  │  MPU-9250        │  0xFF 厂商域 ASCII @ 行        │  sle_seek_print_all     │
  │  paibing_imu     │  ───────────────────────────►  │  → 去重 → 挥拍/CNN UI   │
  └──────────────────┘   100ms 换帧 / 10Hz 有效       └─────────────────────────┘
```

- **拍柄**：100 ms 采样 → 异步 stop/set/start 刷新空口 → 每帧一行 ASCII（与 BLE Notify 相同）
- **主控**：扫描 `paibing_imu` 或 MAC `cc:ad:c9:00:22:xx` → 解析 `0xFF` → **按 `@uptime_ms` 去重** → 业务 10Hz

> ⚠️ **勿再按 `EB 1A 02` 二进制解析**（已废弃，会导致 G/R 等字段错乱）。详见 [固件变更记录](bs20/docs/固件变更记录.md)。

---

## 开发历程摘要

| 阶段 | 拍柄（BS20） | 主控（SS928） |
|------|-------------|---------------|
| **1. 传感基础** | MPU-9250 软件 I2C、Mahony、UART 调试 | — |
| **2. 无线上报** | BLE Notify ASCII；SLE 非连接扫播 | WS73 扫描工具原型 |
| **3. 协议试错** | 长 ASCII → 二进制 `EB 1A` → **回退 ASCII 双份** | 曾按二进制解析 → 数值错乱 |
| **4. 空口刷新** | 100ms 异步 restart（热更新无效） | `sle_imu_bridge` 管道 |
| **5. 去重与解析** | 5ms 广播间隔（重复是正常现象） | MAC+uptime 去重、ASCII 优先 |
| **6. 端侧智能** | — | 1D CNN 六类挥拍、规则 FSM、`both` 模式 |
| **7. 训练 UI** | MAC 01～06 批量烧录 | 单人/班级/对打、多拍柄、YOLO+IMU 触发 |
| **8. 联调优化** | 文档化 GitHub 仓 | 击球灵敏度调钝、参考代码入 `ss928/reference/` |

完整过程与踩坑 → [docs/开发过程与代码要点.md](docs/开发过程与代码要点.md)

---

## 快速开始

### 拍柄同事（BS20）

```bash
# 1. 集成 HiSilicon fbb_bs2x SDK（见 bs20/integration/）
# 2. 在 SDK 根目录编译
bash tools/build_paibing_sle.sh 01   # MAC 末字节 01 → cc:ad:c9:00:22:01
# 3. 烧录 paibing_sle_mac01_all.fwpkg（勿用 fota.fwpkg）
```

→ [bs20/README.md](bs20/README.md) · Agent Skill：[paibing-bs20](.cursor/skills/paibing-bs20/SKILL.md)

### 主控同事（SS928）

```bash
/opt/widget_ui/ws73/sle_imu_bridge.sh start
tail -f /tmp/sle_imu_lines    # 去重后约 10Hz
tail -f /tmp/widget_imu.log    # hit(cnn) / hit(rule)
```

→ [主控对接说明](bs20/docs/主控对接说明.md) · [关键源码索引](ss928/docs/关键源码索引.md) · Agent Skill：[paibing-ss928](.cursor/skills/paibing-ss928/SKILL.md)

### 联调 checklist

| # | 事项 | 文档 |
|---|------|------|
| 1 | ASCII `@` 行解析 | [主控对接说明 §三](bs20/docs/主控对接说明.md) |
| 2 | ADV + Scan Response 取 0xFF | [主控对接说明 §五](bs20/docs/主控对接说明.md) |
| 3 | MAC + uptime 去重 | [IMU解析与去重实现](ss928/docs/IMU解析与去重实现.md) |
| 4 | 验证 10Hz 有效帧 | [主控待办清单](ss928/docs/主控待办清单.md) |
| 5 | 击球灵敏度 | `run.sh` 中 `WIDGET_IMU_RULE_*` |

---

## 数据格式（ASCII，与 BLE 相同）

```
@10222,A-46,+32,+77,G-119,-248,R+169,P-244,M96\n
```

| 段 | 含义 | 换算 |
|----|------|------|
| `@10222` | 开机相对时间 ms | — |
| `A-46,+32,+77` | 加速度 X/Y/Z | ×10 → **mg** |
| `G-119,-248` | 陀螺 X/Y | **dps** |
| `R+169` / `P-244` | Roll / Pitch | ÷10 → **度** |
| `M96` | 三轴合成力度 | ÷100 → **g** |

---

## 仓库结构

```
.
├── README.md
├── docs/                     # 项目级文档 + 开发历程
├── .cursor/skills/           # Cursor Agent（拍柄 / 主控 / 全栈）
│   ├── paibing-bs20/
│   ├── paibing-ss928/
│   └── paibing-project/
├── bs20/                     # BS20 拍柄固件
└── ss928/                    # 主控文档 + reference/ws73 参考代码
```

---

## 文档导航

| 读者 | 推荐阅读 |
|------|----------|
| **主控首次对接** | [主控对接说明](bs20/docs/主控对接说明.md) → [主控待办清单](ss928/docs/主控待办清单.md) |
| **拍柄固件开发** | [bs20/README.md](bs20/README.md) → [代码文件索引](bs20/docs/代码文件索引.md) |
| **联调排障** | [联调快速指南](docs/联调快速指南.md) |
| **答辩 / 交接** | [开发过程与代码要点](docs/开发过程与代码要点.md) |
| **Cursor 二次开发** | [paibing-project Skill](.cursor/skills/paibing-project/SKILL.md) |

---

## 依赖与许可

- **BS20**：基于 [HiSilicon BS2X SDK (fbb_bs2x)](https://gitee.com/HiSpark/fbb_bs2x)，需自行下载后按 `bs20/integration/` 集成。
- **SS928**：完整面板工程依赖海思 SS928V100 MPP SDK + Qt5，本仓以 **文档 + `ss928/reference/`** 提供；完整工程由队伍内部 `version3.0/` 维护。
- 应用层新增代码 Copyright (c) 2026 星羽汇聚参赛队。

---

## 团队

2026 全国大学生嵌入式芯片与系统设计竞赛 · 海思赛题 · SS928 端侧智能
