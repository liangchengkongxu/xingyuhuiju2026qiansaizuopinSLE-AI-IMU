---
name: paibing-bs20
description: >-
  Develop, build, and debug BS20 paibing racket IMU firmware (SLE broadcast
  + BLE notify). Use when editing paibing code, fixing SLE adv packet loss,
  building fwpkg, integrating into fbb_bs2x SDK, or MAC batch burn packages.
---

# BS20 拍柄固件开发 Skill

## 仓库布局

```
bs20/application/paibing/   # 源码：common + sle + ble
bs20/tools/                 # build_paibing_sle.sh, build_paibing_ble.sh
bs20/integration/           # SDK overlay + 集成说明
docs/开发过程与代码要点.md    # 开发历程与决策记录
```

完整编译依赖 HiSilicon **fbb_bs2x** SDK，本仓库只含应用层与集成 patch。

## 架构

- `common/paibing_imu.c`：MPU9250 + Mahony + 100ms 循环
- `paibing_transport_t`：解耦 IMU 与无线
- `sle/`：非连接扫播（量产默认）
- `ble/`：连接 Notify ASCII（调试）

入口：`sle/paibing_app.c` 或 `ble/paibing_app.c` 的 `app_main()` 覆盖 SDK weak 符号。

## 星闪当前策略（勿随意改回）

1. **ASCII 行**（与 BLE Notify 相同），放在 **0xFF 厂商域**
2. **ADV + Scan Response 双份** 相同 ASCII
3. **100ms 异步 restart**（热更新无效，不可省）
4. 广播间隔 **0x28**（5ms）
5. `sle_customize_max_pwr(8,2)` + `announce_tx_power=8`
6. **禁止** 180ms 限流 restart（会导致数据刷新过慢）

主控必须：**ASCII 解析 + @uptime 去重**。见 `bs20/docs/主控对接说明.md`。

关键文件：`bs20/application/paibing/sle/paibing_sle_server_adv.c`

## 构建

在 fbb_bs2x SDK 根目录（先按 integration 集成）：

```bash
bash tools/build_paibing_sle.sh 04   # MAC cc:ad:c9:00:22:04
bash tools/setup_paibing_ble_prebuild.sh
bash tools/build_paibing_ble.sh
```

输出：`src/tools/pkg/fwpkg/bs20/paibing_sle_macXX_all.fwpkg`

**禁止** 用 BurnTool 烧 `fota.fwpkg`（OTA 格式会超时）。

## 编译 Target

| Target | 组件 |
|--------|------|
| `standard-bs20-n1200` | paibing_common + paibing_sle_porting |
| `standard-bs20-n1200-ble` | paibing_common + paibing_ble_porting |

见 `bs20/integration/sdk_overlay/.../config.py`。

## 常见故障

| 现象 | 检查 |
|------|------|
| 有名无数据 | 是否解析 ADV/ScanRsp 的 0xFF ASCII，而非只看设备名 |
| 数值错乱 | 主控是否误用 EB 1A 二进制解析 |
| uptime 卡死 | 是否又在播着时仅热更新而不 restart |
| 日志刷屏 | 主控未按 @uptime 去重（拍柄 5ms 广播正常） |
| 功率无效 | 是否调用 `sle_customize_max_pwr`；20dBm 只是 API 上限 |
| RSSI 80 | 通常是 -80 dBm 弱信号 |

## MAC 批量

改 `sle/mac_config.h` 的 `PAIBING_LOCAL_MAC_B5` 或：

```bash
bash tools/build_paibing_sle.sh 01   # … 06
```

## 主控协议

- 星闪：**ASCII 行**，ADV+ScanRsp 0xFF，**@uptime 去重**，见 `bs20/docs/主控对接说明.md`
- 蓝牙：连接后 ASCII Notify，格式相同

## 修改原则

- 最小 diff，匹配现有命名与结构
- 用户明确要求「只加功率不改发送逻辑」时，不动广播刷新策略
- 不要提交 `.fwpkg`、`__pycache__`、SDK 符号链接目录
