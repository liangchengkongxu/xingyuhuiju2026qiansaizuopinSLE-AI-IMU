# 星羽汇聚 · 2026 嵌赛 · 星闪 IMU 智能羽毛球训练系统

> **全国大学生嵌入式芯片与系统设计竞赛 · 海思 SS928 端侧智能赛题**  
> 基于 **海思 SS928V100 端侧 NPU** + **BS20 星闪拍柄** 的智能羽毛球训练方案：拍柄九轴无连接广播，主控星闪扫描、挥拍检测、1D CNN 动作分类与 YOLO 视觉辅助，支持单人练习、班级同训、双人对打等多种训练模式。

**仓库地址**：https://github.com/liangchengkongxu/xingyuhuiju2026qiansaizuopinSLE-AI-IMU

---

## 目录

- [项目简介](#项目简介)
- [核心能力与创新点](#核心能力与创新点)
- [系统架构](#系统架构)
- [技术实现详解](#技术实现详解)
  - [拍柄端 BS20](#1-拍柄端-bs20)
  - [主控端 SS928](#2-主控端-ss928)
  - [星闪无线协议](#3-星闪无线协议)
  - [IMU 挥拍检测](#4-imu-挥拍检测)
  - [1D CNN 动作分类](#5-1d-cnn-动作分类)
  - [YOLO 视觉辅助](#6-yolo-视觉辅助)
- [训练业务模式](#训练业务模式)
- [端到端数据流](#端到端数据流)
- [开发历程与协议演进](#开发历程与协议演进)
- [快速开始](#快速开始)
- [仓库结构](#仓库结构)
- [文档导航](#文档导航)
- [依赖与许可](#依赖与许可)

---

## 项目简介

传统羽毛球训练依赖教练肉眼观察，难以同时关注多名学员的挥拍动作、球速与发力质量。本项目将 **惯性传感（IMU）**、**星闪（SLE）近场无线** 与 **端侧人工智能** 结合，在拍柄内嵌入九轴传感器并通过星闪非连接广播实时上报；SS928 主控板通过 WS73 星闪模块扫描接收，在本地完成挥拍检测、六类动作识别与训练评分，无需云端、无需与拍柄建立连接，可支持 **40+ 把拍柄** 同场使用。

### 解决的问题

| 痛点 | 本方案 |
|------|--------|
| 多学员同时训练，教练顾不过来 | 班级模式：每人一块实时卡片，自动统计挥拍次数与得分 |
| 动作类型与发力难以量化 | IMU 1D CNN 识别高远/平抽/挑球/放网/发球/杀球，并估算球速与力度 |
| 蓝牙连接数有限、配对繁琐 | 星闪 **非连接扫播**，上电即广播，主控只扫不收 |
| 远距离收包不稳定 | 22B 二进制协议 + ADV/Scan Response 双份 + MAC+uptime 去重 |
| 误触发（手持微动也算击球） | 规则 FSM + CNN 双通道，`both` 模式互补，阈值可配 |

### 硬件组成

| 模块 | 芯片/器件 | 作用 |
|------|-----------|------|
| **智能拍柄** | BS20-N1200 + MPU-9250 | 九轴采集、Mahony 姿态、星闪/BLE 上报 |
| **主控板** | SS928V100（A55 + NPU） | Qt 训练 UI、IMU 解析、端侧 AI 推理 |
| **星闪模块** | WS73 | 非连接扫描、接收拍柄广播 |
| **摄像头**（可选） | MIPI 接入 SS928 | YOLOv5 人体/挥拍视觉辅助 |

拍柄 MAC 前缀：`cc:ad:c9:00:22:01`～`06`，设备名 `paibing_imu`。

---

## 核心能力与创新点

1. **星闪非连接多设备扫播**  
   拍柄无需配对、无需连接，上电即向空口广播 IMU 数据；主控 WS73 持续扫描，按 MAC 区分不同学员/球拍。

2. **远距离二进制协议（7m+ 优化）**  
   载荷仅 22 字节 `EB 1A 02`，比 ASCII 文本更短、CRC 通过率更高，实测 7m 以上收包率明显优于 ASCII 方案。

3. **双层去重保证 10Hz 有效采样**  
   拍柄 10ms 广播间隔 vs 100ms 换帧 → 同一帧会被扫到多次；C 层 `imu_dedup_allow()` + Qt 层 `m_lastSampleTByMac` 双层过滤，业务侧稳定 10Hz。

4. **端侧双模击球检测**  
   - **规则 FSM**：dyn/gyro 阈值 + 三态机（idle→swing→cooldown），低延迟、可解释  
   - **1D CNN**：8 通道 × 24 帧窗口，NPU 推理六类挥拍  
   - **`both` 模式**：CNN 峰值优先，规则补漏，再用 CNN 做动作分类

5. **IMU + 视觉融合**  
   单人练习/对打模式支持摄像头 YOLO 挥拍识别，与 IMU 触发 OR 合并，视觉可在 ±1s 窗口内校正动作类型。

6. **完整训练 UI**  
   单人动作教学、班级多人同训、双人对打计分、训练总结与 AI 改进建议，面向 Embedded 赛题答辩与真实教学场景。

---

## 系统架构

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         BS20 智能拍柄（每把独立）                          │
│  MPU-9250 ──► Mahony AHRS ──► 100ms 主循环 ──► transport.push_sensor() │
│                                    │                                     │
│                    ┌───────────────┴───────────────┐                     │
│                    ▼                               ▼                     │
│           SLE 非连接扫播                    BLE GATT Notify               │
│     22B EB 1A 02 @ 0xFF 厂商域              ASCII 行（调试）              │
│     ADV + Scan Response 双份                                            │
└───────────────────────────────┬─────────────────────────────────────────┘
                                │ 星闪空口（10ms 广播 / 100ms 换帧）
                                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    SS928 主控 + WS73 星闪模块                             │
│                                                                          │
│  sle_seek_print_all ──► TLV 0xFF 解析 ──► EB 1A 优先 ──► @ 行 + 去重    │
│         │                                                                │
│         ▼                                                                │
│  sle_imu_bridge.sh ──► /tmp/sle_imu_lines                               │
│         │                                                                │
│         ▼                                                                │
│  SleImuService ──► ImuSwingDetector (规则) + ImuCnnClassifier (NPU)     │
│         │                                                                │
│         ├──────────────────────┬──────────────────────┐                 │
│         ▼                      ▼                      ▼                 │
│   单人练习 UI            班级同训 UI              对打模式 UI              │
│   + 教学视频             多学员卡片               最多 4 拍柄              │
│   + YOLO 辅助            排名与总结               IMU 触发 + 视觉延迟更新  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 技术实现详解

### 1. 拍柄端 BS20

源码目录：[`bs20/application/paibing/`](bs20/application/paibing/)

#### 1.1 传感与姿态

| 项目 | 实现 |
|------|------|
| 传感器 | MPU-9250，软件 I2C（SCL=MGPIO16, SDA=MGPIO15） |
| 采样周期 | 100ms 主循环（`PAIBING_SEND_INTERVAL_MS`） |
| 姿态解算 | Mahony AHRS → roll / pitch |
| 上电校准 | 20 样本 × 50ms 陀螺零偏，约 1s（请保持静止） |
| 调试串口 | UART0 115200，打印 acc/gyro/roll/pitch/magnitude |

核心文件：[`common/paibing_imu.c`](bs20/application/paibing/common/paibing_imu.c)

#### 1.2 传输抽象

拍柄通过 `paibing_transport_t` 抽象层同时支持星闪与蓝牙两种上报方式，业务层只调用 `push_sensor()`，由 SLE/BLE 各自实现组包：

```
common/paibing_imu.c
    └─► sle/paibing_app.c          星闪非连接扫播（量产）
    └─► ble/paibing_app.c          BLE Notify（调试/单设备验证）
```

#### 1.3 星闪广播策略

| 参数 | 值 | 说明 |
|------|-----|------|
| 设备名 | `paibing_imu` | 扫描过滤 |
| 换帧周期 | 100ms | IMU 新数据 |
| 广播间隔 | 10ms（0x50） | 提高被扫到概率 |
| 载荷位置 | ADV + Scan Response 的 `0xFF` 厂商域 | 双份相同，提高收包率 |
| 空口刷新 | **异步 stop → set_data → start** | 播着时 `set_announce_data` 不刷新，必须 restart |
| 发射功率 | `sle_customize_max_pwr(8,2)` | 约 6～8 dBm |

核心文件：[`sle/paibing_sle_server_adv.c`](bs20/application/paibing/sle/paibing_sle_server_adv.c)

#### 1.4 批量烧录

```bash
# 在 HiSilicon fbb_bs2x SDK 根目录
bash tools/build_paibing_sle.sh 01   # → paibing_sle_mac01_all.fwpkg
bash tools/build_paibing_sle.sh 02   # MAC 末字节 02
# … 支持 01～06
```

**注意**：烧录 `*_all.fwpkg`，**勿用** `fota.fwpkg`（串口会超时）。

→ 详细说明：[bs20/README.md](bs20/README.md)

---

### 2. 主控端 SS928

完整 Qt 工程已纳入本仓 [`ss928/version3.0/`](ss928/version3.0/)（源码，不含 NPU `.om` 与板端二进制）；[`ss928/reference/`](ss928/reference/) 仍保留星闪解析参考快照。

#### 2.1 星闪 IMU 接收链

```
WS73 sle_seek_print_all
  → walk ADV TLV，找 type=0xFF
  → EB 1A 02 二进制优先 → format_imu_sensor_adv() 转 @ 行
  → imu_dedup_allow(mac, uptime_ms) 去重
  → [SLE_IMU] mac=...|@... 打印
  → sle_imu_bridge.sh 重定向 → /tmp/sle_imu_lines
```

参考实现：[`ss928/reference/ws73/sle_seek_print_all/sle_seek_print_client.c`](ss928/reference/ws73/sle_seek_print_all/sle_seek_print_client.c)

#### 2.2 Qt IMU 业务层

| 模块 | 文件 | 职责 |
|------|------|------|
| IMU 服务 | `sle_imu_service.cpp` | 读日志、二次去重、分发击球事件 |
| 规则检测 | `imu_swing_detector.cpp` | 三态 FSM、球速/力度估算 |
| CNN 分类 | `imu_cnn_classifier.cpp` | 峰值触发 + 六类动作 NPU 推理 |
| 评分建议 | `class_hit_ai_advice.cpp` | 按动作类型 + CNN 置信度 + 力度给改进建议 |

#### 2.3 部署

```bash
# 星闪解析工具（改 C 后执行）
bash ss928/version3.0/scripts/deploy_ws73.sh

# Qt 面板增量编译（改 cpp 后执行）
bash ss928/version3.0/scripts/deploy_panel.sh sle_imu_service.cpp imu_swing_detector.cpp

# AI 进程（Pose / 回放改 sample_vio_ai.c 后）
bash ss928/version3.0/scripts/deploy_bin.sh
```

→ 详细说明：[ss928/docs/部署与运行.md](ss928/docs/部署与运行.md)

---

### 3. 星闪无线协议

**当前量产协议（2026-07，commit `499a3735`）**：22 字节二进制，权威文档 → [主控对接说明-远距离二进制版](bs20/docs/主控对接说明-远距离二进制版.md)

#### 3.1 空口帧格式

```
EB 1A 02 DEV | uptime(2 LE) | ax,ay,az(2×3 LE) | gx,gy(2×2 LE) | roll,pitch(2×2 LE) | mag(2 LE)
```

| 偏移 | 字段 | 换算 |
|------|------|------|
| 0-2 | `EB 1A 02` | 帧头 |
| 4-5 | uptime | u16 LE，毫秒 |
| 6-11 | ax, ay, az | i16 LE，×10 → mg |
| 12-15 | gx, gy | i16 LE，dps |
| 16-19 | roll, pitch | i16 LE，÷10 → 度 |
| 20-21 | magnitude | u16 LE，÷100 → g |

#### 3.2 主控解析优先级

```
收到 SLE 扫描报告
  ├─ ① TLV type=0xFF → EB 1A 02（量产默认）
  ├─ ② 整包扫描 EB 1A
  └─ ③ ASCII @ 行（BLE Notify / 旧固件回退）
```

**切勿对二进制载荷做 sscanf ASCII**，否则 G/R/P 会出现巨大乱数。

#### 3.3 内部 @ 行（供 Qt 统一解析）

主控将二进制转为：

```
@10222,A-46,+32,+77,G-119,-248,R+169,P-244,M96
```

| 段 | 含义 | 下游换算 |
|----|------|----------|
| `@10222` | uptime ms | 去重键 |
| `A-46,+32,+77` | 加速度 | centi-g，×10 → mg |
| `G-119,-248` | 陀螺 | dps |
| `R+169,P-244` | 姿态 | ÷10 → 度 |
| `M96` | 合成力度 | ÷100 → g |

→ 去重实现：[ss928/docs/IMU解析与去重实现.md](ss928/docs/IMU解析与去重实现.md)

---

### 4. IMU 挥拍检测

击球检测在 `SleImuService::pollImuLog()` 中按 `WIDGET_IMU_HIT_MODE` 执行：

| 模式 | 行为 |
|------|------|
| `rule` | 仅规则 FSM |
| `cnn` | 仅 CNN 峰值触发 |
| `both`（默认） | CNN 优先；未命中则规则补漏，再用 CNN 分类动作类型 |

#### 4.1 规则 FSM（`imu_swing_detector.cpp`）

三态机：`idle` → `swing` → `cooldown` → `idle`

**触发条件**（满足任一 + 连续确认帧）：

| 条件 | 含义 |
|------|------|
| accTrigger | dyn ≥ 阈值（M 相对基线增量，默认 0.06g） |
| gyroTrigger | 陀螺 ≥ 35°/s 且 dyn 辅助达标 |
| stepTrigger | 单帧 M 跳变 + 陀螺/dyn 辅助 |

**结束条件**：dyn 回落 < 0.05g 且持续 ≥ 33ms，或超时 650ms。

**输出**：球速 km/h、力度 1～10、峰值 dyn/gyro、挥拍时长。

环境变量（`run.sh`）：

| 变量 | 默认 | 含义 |
|------|------|------|
| `WIDGET_IMU_RULE_SWING_ON_DYN` | 6 | 触发 dyn（centi-g） |
| `WIDGET_IMU_RULE_GYRO_ON` | 35 | 陀螺触发 °/s |
| `WIDGET_IMU_RULE_CONFIRM` | 1 | 连续确认帧 |
| `WIDGET_IMU_RULE_COOLDOWN_MS` | 300 | 两次击球最小间隔 |

#### 4.2 CNN 峰值检测（`imu_cnn_classifier.cpp`）

- 64 帧环形缓冲，找 M 值局部峰
- **硬触发**：局部峰 + M/dyn 达标 + 置信度 ≥ 22%
- **软触发**：略低阈值 + 置信度 ≥ 18%
- 冷却：500ms

---

### 5. 1D CNN 动作分类

| 项目 | 说明 |
|------|------|
| 模型 | `badminton_npu.om`，SS928 NPU 推理 |
| 输入 | 8 通道 × 24 帧窗口（acc/gyro/姿态等，与训练预处理一致） |
| 输出 | 六类挥拍 + Softmax 置信度 |

| classId | 动作 |
|---------|------|
| 0 | 高远 |
| 1 | 平抽 |
| 2 | 挑球 |
| 3 | 放网 |
| 4 | 发球 |
| 5 | 杀球 |

**班级模式评分**：50 分保底 + 最多 49 分（CNN 匹配度 × 动作适宜力度），见 `classHitScoreFromImu()`。

---

### 6. YOLO 视觉辅助

| 项目 | 说明 |
|------|------|
| 模型 | YOLOv5s，NCNN/NPU 端侧推理 |
| 用途 | 检测场上挥拍动作，与 IMU 触发 OR 合并 |
| 对打模式 | IMU 触发后 ±1s 窗口查询视觉最佳匹配，延迟更新动作类型与评分 |
| 环境变量 | `WIDGET_HIT_SOURCE=both` / `imu` / `camera` |

---

## 训练业务模式

| 模式 | 入口 | 功能要点 |
|------|------|----------|
| **单人练习** | 首页 → 动作选择 | 教学视频 + IMU/摄像头击球计数 + 每次得分 |
| **班级同训** | 多人模式 → 扫描绑定 → 班级同训 | 多学员大卡片实时显示挥拍/得分；停止后班级总结排名 |
| **双人对打** | 对打模式 | 最多 4 台拍柄绑定；分屏布局；IMU 触发 + 视觉校正 |
| **训练总结** | 训练中「查看总结」 | 每次挥拍横条列表；点击进入动作详情与 AI 建议 |

班级模式数据流：

```
拍柄 IMU 广播 → SleImuService::hitDetected
  → ClassTrainPage::onImuHitDetected
  → classHitScoreFromImu() 评分
  → 学员卡片实时刷新（平均得分、挥拍次数、最近动作类型）
```

---

## 端到端数据流

```
[BS20] MPU-9250 采样 (100ms)
    ↓
[BS20] 组 22B EB 1A 02 → ADV + ScanRsp 0xFF
    ↓ 星闪空口 (10ms 重复广播)
[WS73] sle_seek_print_all 扫描
    ↓ TLV 解析 + EB 1A 优先 + MAC+uptime 去重
[WS73] 输出 [SLE_IMU] @ 行 → /tmp/sle_imu_lines
    ↓
[Qt] SleImuService::pollImuLog (二次去重)
    ↓
[Qt] ImuCnnClassifier::tryDetectHit (CNN 峰值)
    ↓ 未命中
[Qt] ImuSwingDetector::feed (规则 FSM)
    ↓ 命中
[Qt] ImuCnnClassifier::classifySwing (动作分类)
    ↓
[Qt] emit hitDetected → UI 更新 / 班级统计 / 对打计分
```

**板端验证命令**：

```bash
/opt/widget_ui/ws73/sle_imu_bridge.sh start
tail -f /tmp/sle_imu_lines          # 期望 ~10Hz，uptime 递增，不重复刷屏
tail -f /tmp/widget_imu.log | grep hit   # hit(cnn) / hit(rule)
```

---

## 开发历程与协议演进

| 阶段 | 拍柄（BS20） | 主控（SS928） |
|------|-------------|---------------|
| **1. 传感基础** | MPU-9250 软件 I2C、Mahony、UART 调试 | — |
| **2. 无线上报** | BLE Notify；SLE 非连接扫播原型 | WS73 扫描工具 |
| **3. 协议试错** | 长 ASCII → 22B 二进制 → ASCII → **再回 22B 二进制** | 解析顺序同步迭代 |
| **4. 空口刷新** | 100ms 异步 restart（热更新无效） | `sle_imu_bridge` 管道 |
| **5. 去重** | 10ms 广播（重复是正常现象） | MAC+uptime 双层去重 |
| **6. 端侧智能** | — | 1D CNN 六类 + 规则 FSM + `both` |
| **7. 训练 UI** | MAC 01～06 批量烧录 | 单人/班级/对打、YOLO 融合 |
| **8. 工程化** | GitHub 文档化、Skill | 参考代码入 `ss928/reference/` |
| **9. Pose + 回放** | — | RGN 骨骼、960×540 击球回放、ch0 抢帧踩坑修复 |

### 协议选型结论

| 方案 | 优点 | 缺点 | 结论 |
|------|------|------|------|
| ADV 内长 ASCII (~50B) | 易调试 | 2m 外丢包严重 | 弃用 |
| 22B `EB 1A 02` 二进制 | 包短、7m+ 收包好 | 主控须按字节解析 | **当前量产** |
| ASCII 双份 | 5m 内直观 | 7m+ 收包率下降 | 已回退 |

完整踩坑记录 → [docs/开发过程与代码要点.md](docs/开发过程与代码要点.md)

---

## 快速开始

### 拍柄（BS20）

```bash
# 1. 集成 HiSilicon fbb_bs2x SDK（见 bs20/integration/）
# 2. 在 SDK 根目录编译
bash tools/build_paibing_sle.sh 01
# 3. 烧录 paibing_sle_mac01_all.fwpkg
```

→ [bs20/README.md](bs20/README.md) · Skill：[paibing-bs20](.cursor/skills/paibing-bs20/SKILL.md)

### 主控（SS928）

```bash
/opt/widget_ui/ws73/sle_imu_bridge.sh start
tail -f /tmp/sle_imu_lines
cd /opt/widget_ui && ./run.sh
```

→ [主控对接说明-远距离二进制版](bs20/docs/主控对接说明-远距离二进制版.md) · Skill：[paibing-ss928](.cursor/skills/paibing-ss928/SKILL.md)

### 联调 Checklist

| # | 事项 | 文档 |
|---|------|------|
| 1 | 22B `EB 1A 02` 解析 | [主控对接说明-远距离二进制版](bs20/docs/主控对接说明-远距离二进制版.md) |
| 2 | ADV + Scan Response 取 0xFF | 同上 |
| 3 | MAC + uptime 去重 | [IMU解析与去重实现](ss928/docs/IMU解析与去重实现.md) |
| 4 | 验证 10Hz 有效帧 | [主控待办清单](ss928/docs/主控待办清单.md) |
| 5 | 击球灵敏度 | `run.sh` 中 `WIDGET_IMU_RULE_*` |
| 6 | 端到端联调 | [联调快速指南](docs/联调快速指南.md) |

---

## 仓库结构

```
.
├── README.md                          # 本文件 · 项目总览
├── docs/                              # 项目级文档
│   ├── 开发过程与代码要点.md            # 答辩 / 交接 / 踩坑全集
│   ├── 联调快速指南.md
│   └── 仓库目录说明.md
├── .cursor/skills/                      # Cursor Agent 二次开发
│   ├── paibing-project/               # 全栈入口
│   ├── paibing-bs20/                  # 拍柄固件
│   └── paibing-ss928/                 # 主控 IMU / UI
├── bs20/                              # BS20 拍柄固件
│   ├── application/paibing/           # IMU + SLE + BLE 源码
│   ├── tools/                         # build_paibing_sle.sh 等
│   ├── integration/                   # SDK overlay 集成
│   └── docs/                          # 主控对接说明、固件变更记录
└── ss928/                             # SS928 主控
    ├── version3.0/                    # ★ 完整 Qt + AI 工程源码
    ├── docs/                          # 部署、解析、待办清单
    └── reference/ws73/                # sle_seek_print_client.c 参考快照
```

---

## 文档导航

| 读者 | 推荐阅读顺序 |
|------|-------------|
| **评委 / 首次了解** | 本 README → [开发过程与代码要点](docs/开发过程与代码要点.md) |
| **主控同事对接** | [主控对接说明-远距离二进制版](bs20/docs/主控对接说明-远距离二进制版.md) → [主控待办清单](ss928/docs/主控待办清单.md) → [IMU解析与去重](ss928/docs/IMU解析与去重实现.md) |
| **拍柄同事开发** | [bs20/README.md](bs20/README.md) → [代码文件索引](bs20/docs/代码文件索引.md) → [固件变更记录](bs20/docs/固件变更记录.md) |
| **联调排障** | [联调快速指南](docs/联调快速指南.md) |
| **Cursor 二次开发** | [paibing-project Skill](.cursor/skills/paibing-project/SKILL.md) |

---

## 依赖与许可

| 组件 | 依赖 | 说明 |
|------|------|------|
| **BS20 拍柄** | [HiSilicon fbb_bs2x SDK](https://gitee.com/HiSpark/fbb_bs2x) | 需自行下载，按 `bs20/integration/` 集成 |
| **SS928 主控** | SS928V100 MPP SDK + Qt5 + WS73 原厂 SDK | 完整工程见本仓 `ss928/version3.0/`；NPU 模型随板端 `/opt/widget_ui/models/` 部署 |
| **NPU 模型** | `badminton_npu.om` | 随完整工程部署至 `/opt/widget_ui/models/` |

- 应用层新增代码 Copyright (c) 2026 **星羽汇聚参赛队**
- BS20 底层驱动与协议栈遵循 HiSilicon SDK 许可

---

## 团队

**星羽汇聚** · 2026 全国大学生嵌入式芯片与系统设计竞赛 · 海思 SS928 端侧智能赛题
