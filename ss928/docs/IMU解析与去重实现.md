# IMU 解析与去重实现

说明 SS928 主控如何解析拍柄星闪广播，以及与 GitHub 协议文档的对应关系。

参考代码：`../reference/ws73/sle_seek_print_all/sle_seek_print_client.c`

---

## 1. 解析优先级

```
收到一次 SLE 扫描报告 (info->data)
    │
    ├─► ① walk ADV TLV，找 type=0xFF
    │       ├─ payload[0]=='@' → ASCII 行（当前固件）
    │       ├─ 缓冲区内搜 '@' → ASCII 行
    │       └─ EB 1A 02 → 二进制回退（旧固件）
    │
    ├─► ② 整包扫描 '@' 行
    │
    └─► ③ 整包扫描 EB 1A 二进制
```

**当前拍柄固件**走 ① 的 ASCII 分支；勿再默认走二进制。

---

## 2. ASCII 行校验

有效行需包含：`,A` `,G` `,R` `,P` 及 `,M` + 数字，以 `@` 开头。

示例：

```
@10222,A-46,+32,+77,G-119,-248,R+169,P-244,M96
```

---

## 3. 去重逻辑

### 为何要去重

拍柄广播间隔 **5ms**，IMU 换帧 **100ms** → 同一 `@uptime` 会被扫到 **2～20 次**。

### C 层（`sle_seek_print_all`）

```c
/* 每个 MAC 记录 last_uptime_ms */
static sle_imu_dedup_slot_t g_imu_dedup[16];

int imu_dedup_allow(const char *mac, uint32_t uptime_ms)
{
    /* 同 MAC + 同 uptime → 返回 0，不打印 */
    /* 新 uptime → 更新表，返回 1 */
}
```

在 `emit_imu_line_tagged()` 输出 `[SLE_IMU]` 前调用。

### Qt 层（`sle_imu_service.cpp`）

读 `/tmp/sle_imu_lines` 时二次过滤：

```cpp
if (m_lastSampleTByMac.value(macKey, -1) == sample.tMs)
    return;
m_lastSampleTByMac.insert(macKey, sample.tMs);
```

防止桥接重启前残留重复行进入挥拍 FSM。

---

## 4. 下游解析（`imu_swing_detector.cpp`）

`parseImuLine()` 规则：

| 字段 | 星闪 ASCII | 说明 |
|------|------------|------|
| A | centi-g | 若 \|A\|≤300 则 ×10 得 mg |
| G | dps | 直接用于挥拍 |
| R/P | ×0.1° | |
| M | ×0.01 g | |

与 [主控对接说明 §三](../../bs20/docs/主控对接说明.md) 一致。

---

## 5. 数据通路

```
BS20 广播
    → WS73 sle_seek_print_all
    → [SLE_IMU] mac=...|@...
    → sle_imu_bridge.sh → /tmp/sle_imu_lines
    → SleImuService::pollImuLog()
    → ImuSwingDetector / ImuCnnClassifier
    → UI hitDetected
```

---

## 6. 调试

```bash
# 关闭安静模式，看原始扫描
unset SLE_SEEK_QUIET
/opt/widget_ui/ws73/sle_seek_print_all

# IMU 业务日志
tail -f /tmp/widget_imu.log
```

环境变量 `WIDGET_IMU_CNN_DEBUG=1` 可打印 CNN 六类概率。
