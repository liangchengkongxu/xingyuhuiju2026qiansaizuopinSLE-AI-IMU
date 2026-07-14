---
name: paibing-bs20
description: >-
  Develop, build, and debug BS20 paibing racket IMU firmware (SLE 22B binary
  broadcast + BLE ASCII notify). Use when editing paibing code, fixing SLE adv
  building fwpkg, integrating into fbb_bs2x SDK, or MAC batch burn packages.
  For SS928 parse/dedup/hit detection use paibing-ss928 skill.
---

# BS20 拍柄固件开发 Skill

## 仓库布局

```
bs20/application/paibing/   # 源码：common + sle + ble
bs20/tools/                 # build_paibing_sle.sh, build_paibing_ble.sh
bs20/integration/           # SDK overlay + 集成说明
bs20/docs/                  # 主控对接说明、固件变更记录
docs/开发过程与代码要点.md    # 全栈开发历程
```

完整编译依赖 HiSilicon **fbb_bs2x** SDK，本仓库只含应用层与集成 patch。

## 架构

```
common/paibing_imu.c          # MPU9250 + Mahony + 100ms 循环
        │
        │ paibing_transport_t { init, push_sensor }
        ├─► sle/paibing_app.c           # 星闪非连接扫播（量产）
        └─► ble/paibing_app.c           # BLE Notify ASCII（调试）
```

入口：`sle/paibing_app.c` 或 `ble/paibing_app.c` 的 `app_main()` 覆盖 SDK weak 符号。

## 协议演进（勿搞混历史方案）

| 阶段 | 载荷 | 状态 |
|------|------|------|
| 早期 | ADV 内长 ASCII | 已弃用（远距离丢包） |
| 中期 | 22B `EB 1A 02` 二进制 | 7m+ 收包优先 |
| 短暂 | ASCII `@` 行双份 | 7m+ 收包率下降 |
| **当前** | **22B 二进制**，ADV+ScanRsp 双份 | **量产**（499a3735） |

主控对接见 `bs20/docs/主控对接说明-远距离二进制版.md`；主控实现见 `ss928/` 与 [paibing-ss928](../paibing-ss928/SKILL.md)。

## 星闪当前策略（勿随意改回）

1. **22B 二进制** `EB 1A 02`，放在 **0xFF 厂商域**
2. **ADV + Scan Response 双份** 相同载荷
3. **100ms 异步 restart**（播着时 `set_announce_data` 不刷新空口，不可省）
4. 广播间隔 **0x50**（10ms）→ 主控会扫到重复帧，**靠主控 uptime 去重**
5. `sle_customize_max_pwr(8,2)` + `announce_tx_power=8`
6. **禁止** 180ms 限流 restart（数据刷新过慢）

关键文件：`bs20/application/paibing/sle/paibing_sle_server_adv.c`

### ASCII 行示例

```
@10222,A-46,+32,+77,G-119,-248,R+169,P-244,M96\n
```

A×10=mg，G=dps，R/P÷10=°，M÷100=g。

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

## 常见故障

| 现象 | 检查 |
|------|------|
| 有名无数据 | 主控是否解析 ADV/ScanRsp 的 0xFF，而非只看设备名 |
| 主控 G/R 乱数 | 主控是否误用 ASCII sscanf 解二进制 |
| uptime 卡死 | 是否又在播着时仅热更新而不 restart |
| 主控日志刷屏 | 主控未按 @uptime 去重（拍柄 5ms 广播是正常现象） |
| 功率无效 | 是否调用 `sle_customize_max_pwr` |
| RSSI 80 | 通常是 -80 dBm 弱信号 |
| 校准后 G/R/P 为 0 一帧 | `paibing_imu.c` 校准完成故意发零帧，正常 |

## MAC 批量

```bash
bash tools/build_paibing_sle.sh 01   # … 06 → cc:ad:c9:00:22:01…06
```

## 与主控联调

拍柄改协议后必须通知主控同步：

1. `bs20/docs/主控对接说明.md`
2. `bs20/docs/固件变更记录.md`
3. `ss928/reference/ws73/sle_seek_print_client.c` 快照

## 修改原则

- 最小 diff，匹配现有命名与结构
- 用户明确要求「只加功率不改发送逻辑」时，不动广播刷新策略
- 不要提交 `.fwpkg`、`__pycache__`、SDK 符号链接目录
- 协议变更写进 `固件变更记录.md`
