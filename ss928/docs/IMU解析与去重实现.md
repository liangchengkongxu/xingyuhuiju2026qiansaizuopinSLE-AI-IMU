# IMU 解析与去重实现

说明 SS928 主控如何解析拍柄星闪广播。参考代码：`../reference/ws73/sle_seek_print_all/sle_seek_print_client.c`

协议权威文档：[主控对接说明-远距离二进制版](../../bs20/docs/主控对接说明-远距离二进制版.md)

---

## 1. 解析优先级（2026-07 远距离二进制版）

```
收到 SLE 扫描报告
    │
    ├─► ① walk TLV type=0xFF
    │       ├─ EB 1A 02 → 22B 二进制（量产默认）
    │       └─ @ 开头 → ASCII（BLE / 旧星闪固件）
    ├─► ② 整包扫描 EB 1A
    └─► ③ 整包扫描 ASCII @ 行
```

**当前拍柄固件**走 ① 的二进制分支；**勿对二进制做 sscanf**。

二进制转内部 `@` 行时，A 字段用 **centi-g**（mg÷10），与 `imu_swing_detector.cpp` 兼容。

---

## 2. 22 字节帧（摘要）

| 偏移 | 字段 | 换算 |
|------|------|------|
| 0-2 | `EB 1A 02` | 头 |
| 4-5 | uptime | u16 LE，毫秒 |
| 6-11 | ax,ay,az | i16 LE，×10 → mg |
| 12-15 | gx,gy | i16 LE，dps |
| 16-19 | roll,pitch | i16 LE，÷10 → 度 |
| 20-21 | magnitude | u16 LE，÷100 → g |

实现见 `format_imu_sensor_adv()` → `imu_format_line()`。

---

## 3. 去重逻辑

拍柄广播间隔 **10ms**，IMU 换帧 **100ms** → 同一 uptime 重复多次。

- **C 层**：`imu_dedup_allow(mac, uptime_ms)`，在 `emit_imu_line_tagged()` 前调用
- **Qt 层**：`m_lastSampleTByMac` in `sle_imu_service.cpp`

去重键：**MAC + uptime_ms**（二进制偏移 4～5，或 ASCII `@` 后数字）。

---

## 4. 下游解析（`imu_swing_detector.cpp`）

内部统一为 `@` 行后：

| 字段 | 说明 |
|------|------|
| A | centi-g；\|A\|≤300 时 ×10 得 mg |
| G | dps |
| R/P | ×0.1° |
| M | ×0.01 g |

---

## 5. 数据通路

```
BS20 22B 广播 → sle_seek_print_all → [SLE_IMU] @ 行
  → sle_imu_bridge.sh → /tmp/sle_imu_lines
  → SleImuService → ImuSwingDetector / ImuCnnClassifier → UI
```

---

## 6. 调试

```bash
/opt/widget_ui/ws73/sle_imu_bridge.sh start
tail -f /tmp/sle_imu_lines
unset SLE_SEEK_QUIET && /opt/widget_ui/ws73/sle_seek_print_all   # 看 [SLE_IMU_RAW] hex
tail -f /tmp/widget_imu.log
```

正常：`eb 1a 02 ...` 转出的 `@` 行 A/G/R/P 与拍柄 UART 量级一致；uptime 约 100ms 递增。
