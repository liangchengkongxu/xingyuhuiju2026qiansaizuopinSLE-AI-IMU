---
name: paibing-ss928
description: >-
  Develop and debug SS928 main controller IMU pipeline: WS73 SLE scan, ASCII
  parse, MAC+uptime dedup, swing detection, 1D CNN classify, Qt widget_panel
  deploy. Use when editing sle_seek_print_client, sle_imu_service, imu_swing_detector,
  deploy_ws73.sh, run.sh env vars, or fixing false hit triggers.
---

# SS928 主控 IMU / 训练面板 Skill

## 仓库布局（本 GitHub 仓）

```
ss928/docs/                  # 对接、部署、去重说明
ss928/reference/ws73/        # sle_seek_print_client.c 参考快照
```

完整 Qt 工程在队伍内部 SDK 工作区 `version3.0/`（未整包入 Git）。

## 数据通路

```
BS20 广播 (ASCII @ 行, 5ms 间隔, 100ms 换帧)
  → sle_seek_print_all (解析 0xFF, MAC+uptime 去重)
  → [SLE_IMU] mac=...|@...
  → sle_imu_bridge.sh → /tmp/sle_imu_lines
  → SleImuService::pollImuLog (二次去重)
  → ImuSwingDetector (规则 FSM) + ImuCnnClassifier (1D CNN)
  → UI hitDetected
```

## 解析规则（当前，勿回退二进制默认）

1. **ASCII 优先**：厂商 0xFF 内 `@uptime,A...,G...,R...,P...,M...`
2. **ADV + Scan Response** 均尝试 TLV 解析
3. **去重**：同 MAC + 同 `@uptime_ms` 只输出/处理一次
4. `EB 1A 02` 仅旧固件回退

关键文件（完整工程）：

| 文件 | 职责 |
|------|------|
| `ws73/sle_seek_print_all/sle_seek_print_client.c` | ★ 扫描解析 + 去重 |
| `ws73/sle_imu_bridge.sh` | 守护扫描 → `/tmp/sle_imu_lines` |
| `src/sle_imu_service.cpp` | 读日志、击球分发、CNN/规则 |
| `src/imu_swing_detector.cpp` | 规则 FSM、速度/力度 |
| `src/imu_cnn_classifier.cpp` | NPU 1D CNN 触发 + 分类 |
| `scripts/run.sh` | 环境变量默认值 |
| `scripts/deploy_ws73.sh` | 部署星闪工具 |
| `scripts/deploy_panel.sh` | 增量编译 Qt |

本仓参考副本：`ss928/reference/ws73/`

## ASCII 字段换算

```
@10222,A-46,+32,+77,G-119,-248,R+169,P-244,M96
```

| 段 | 换算 |
|----|------|
| A | ×10 → mg |
| G | dps |
| R/P | ÷10 → 度 |
| M | ÷100 → g |

`parseImuLine()`：若 |A|≤300 视为 centi-g 再 ×10。

## 击球检测模式

`WIDGET_IMU_HIT_MODE`（run.sh）：

| 值 | 行为 |
|----|------|
| `both` | CNN 峰值优先，未中则规则 FSM + CNN 分类 |
| `rule` | 仅规则 |
| `cnn` | 仅 CNN |

日志：`/tmp/widget_imu.log` 中 `hit(cnn)` / `hit(rule)`

### 灵敏度（已调钝，2026-07）

| 变量 | 默认 | 含义 |
|------|------|------|
| `WIDGET_IMU_RULE_SWING_ON_DYN` | 8 | 触发 dyn（0.08g） |
| `WIDGET_IMU_RULE_GYRO_ON` | 45 | 陀螺触发 °/s |
| `WIDGET_IMU_RULE_CONFIRM` | 2 | 连续确认帧 |
| `WIDGET_IMU_RULE_COOLDOWN_MS` | 700 | 规则冷却 |
| `WIDGET_IMU_CNN_TRIGGER_MIN_DYN` | 8 | CNN 硬触发 dyn |
| `WIDGET_IMU_CNN_TRIGGER_CONF` | 25 | CNN 置信 % |

太灵敏 → 增大 SWING_ON_DYN / GYRO_ON / CONFIRM。  
太钝 → 反向微调。

## 板端部署

```bash
# 仅星闪解析
bash version3.0/scripts/deploy_ws73.sh

# 仅 Qt（指定 cpp）
bash version3.0/scripts/deploy_panel.sh sle_imu_service.cpp imu_swing_detector.cpp

# 重启
ssh root@192.168.1.168 "/opt/widget_ui/ws73/sle_imu_bridge.sh stop; \
  /opt/widget_ui/ws73/sle_imu_bridge.sh start; killall widget_panel; \
  cd /opt/widget_ui && ./run.sh"
```

板端 IP 默认 `192.168.1.168`，目录 `/opt/widget_ui/`。

## 验证

```bash
tail -f /tmp/sle_imu_lines      # 去重后约 10Hz，uptime 递增
tail -f /tmp/widget_imu.log | grep hit
```

正常：相邻 uptime 差 ~100ms；同一 uptime 不刷屏。

## 常见故障

| 现象 | 检查 |
|------|------|
| G/R 巨大乱数 | 是否误用 EB 1A 二进制解析 |
| 同一 @ 重复多行 | 去重未生效 / 桥接未更新 |
| uptime 卡死 | 拍柄广播未 restart |
| 随便动就击球 | 降低灵敏度见上表 |
| 扫不到设备 | WS73 ko、WiFi 共存、`WIDGET_WS73_BSLE_MAX_COEX` |
| CNN 不工作 | `badminton_npu.om`、AICPU 软链、`WIDGET_IMU_CNN_DISABLE` |

## 相关文档

- 协议：[bs20/docs/主控对接说明.md](../../bs20/docs/主控对接说明.md)
- 去重：[ss928/docs/IMU解析与去重实现.md](../../ss928/docs/IMU解析与去重实现.md)
- 联调：[docs/联调快速指南.md](../../docs/联调快速指南.md)

## 修改原则

- 解析改动优先改 `sle_seek_print_client.c`，再部署 `deploy_ws73.sh`
- 击球逻辑改 `imu_swing_detector.cpp` / `imu_cnn_classifier.cpp`，再 `deploy_panel.sh`
- 阈值优先改 `run.sh` 环境变量，便于板端快速试验
- 与拍柄协议变更同步更新 `ss928/reference/` 快照
