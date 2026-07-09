---
name: paibing-bs20
description: >-
  Develop, build, and debug BS20 paibing racket IMU firmware (SLE broadcast
  + BLE notify). Use when editing paibing code, fixing SLE adv packet loss,
  building fwpkg, integrating into fbb_bs2x SDK, MAC batch burn packages,
  or updating host integration docs.
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

- `common/paibing_imu.c`：MPU9250 + Mahony + 100ms 主循环
- `paibing_transport_t`：解耦 IMU 与无线
- `sle/`：非连接扫播（量产默认）
- `ble/`：连接 Notify ASCII（调试）

入口：`sle/paibing_app.c` 或 `ble/paibing_app.c` 的 `app_main()` 覆盖 SDK weak 符号。

## 星闪当前策略（2026-07，勿随意改回）

1. **ASCII 行**（与 BLE Notify 相同），放在 **0xFF 厂商域**
2. **ADV + Scan Response 双份** 相同 ASCII
3. **100ms 同步 restart**：`stop` → 等 disable（≤40ms）→ `commit + start`（热更新无效）
4. 广播间隔 **0x28**（5ms，提高发包密度；0x50 实测丢包更差）
5. `sle_customize_max_pwr(8,2)` + `announce_tx_power=8`
6. **禁止** 单独改二进制而不恢复 0x28（7m 优化失败：发包密度比包长短更关键）

关键文件：`bs20/application/paibing/sle/paibing_sle_server_adv.c`

## 22B 帧布局

```
EB 1A 02 DEV  uptime(2)  ax ay az  gx gy  roll pitch  mag(2)
```

| 偏移 | 字段 | 换算 |
|------|------|------|
| 4-5 | uptime | uint16 LE，ms 低 16 位 |
| 6-11 | ax,ay,az | i16 LE，×10 → mg |
| 12-15 | gx,gy | i16 LE，dps |
| 16-19 | roll,pitch | i16 LE，÷10 → 度 |
| 20-21 | magnitude | u16 LE，÷100 → g |

## 构建

在 fbb_bs2x SDK 根目录（先按 integration 集成）：

```bash
bash tools/build_paibing_sle.sh 04   # MAC cc:ad:c9:00:22:04
bash tools/setup_paibing_ble_prebuild.sh
bash tools/build_paibing_ble.sh
```

输出：`src/tools/pkg/fwpkg/bs20/paibing_sle_macXX_all.fwpkg`（01～08）

**禁止** 用 BurnTool 烧 `fota.fwpkg`（OTA 格式会超时）。

## 编译 Target

| Target | 组件 |
|--------|------|
| `standard-bs20-n1200` | paibing_common + paibing_sle_porting |
| `standard-bs20-n1200-ble` | paibing_common + paibing_ble_porting |

## 常见故障

| 现象 | 检查 |
|------|------|
| 有名无数据 | 是否解析 ADV/ScanRsp 的 0xFF，而非只看设备名 |
| 数值错乱（A+980） | 主控是否误用 ASCII sscanf 解析二进制 |
| uptime 卡死 | 是否又在播着时仅热更新而不 restart |
| restart fail 0x8000600a | 同步 stop/start 抢状态；用异步 disable 回调 |
| 5m 尚可 7m 差 | 优先恢复 0x28 发包密度；7m 还需主控加大扫描占空比 |
| 数值错乱（A+980） | 固件/主控 ASCII 与二进制解析不一致 |
| RSSI 80 | 通常是 -80 dBm 弱信号 |

## 已试验方案（勿重复踩坑）

| 方案 | 问题 |
|------|------|
| 22B 二进制 + 0x50 间隔 | 实测丢包率不如 ASCII+0x28（发包密度更关键） |
| 热更新 `set_announce_data` | 空口不刷新，uptime 卡首帧 |
| 同步 stop→sleep→start | disable 跟不上，0x8000600a |
| 180ms 限流 restart | 破坏 10Hz，同一帧重复过久 |
| 间隔 0x28（5ms） | 可能低于协议栈最小值 |
| 仅 ADV 无 ScanRsp 数据 | 远距离「有名无数据」 |

## MAC 批量

改 `sle/mac_config.h` 的 `PAIBING_LOCAL_MAC_B5` 或：

```bash
bash tools/build_paibing_sle.sh 01   # … 08
```

## Git 提交（本机无 git config 时）

```bash
GIT_AUTHOR_NAME="liangchengkongxu" \
GIT_AUTHOR_EMAIL="liangchengkongxu@users.noreply.github.com" \
GIT_COMMITTER_NAME="liangchengkongxu" \
GIT_COMMITTER_EMAIL="liangchengkongxu@users.noreply.github.com" \
git commit -m "message"
```

## 主控协议

- 星闪：**ASCII 行**，ADV+ScanRsp 0xFF，**@uptime 去重**
- 蓝牙：连接后 ASCII Notify，格式 `@ms,A...,G...,R...,P...,M...\n`

## 修改原则

- 最小 diff，匹配现有命名与结构
- 用户明确要求「只加功率不改发送逻辑」时，不动广播刷新策略
- 不要提交 `.fwpkg`、`__pycache__`、SDK 符号链接目录

## 延伸阅读

- 开发历程：[`docs/开发过程与代码要点.md`](../../docs/开发过程与代码要点.md)
- 踩坑时间线：[`reference.md`](reference.md)
- 主控对接：[`bs20/docs/主控对接说明-远距离二进制版.md`](../../bs20/docs/主控对接说明-远距离二进制版.md)
