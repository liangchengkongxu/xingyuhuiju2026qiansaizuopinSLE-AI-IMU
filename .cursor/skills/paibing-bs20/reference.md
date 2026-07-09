# BS20 拍柄开发历程参考

按时间线记录「试过什么、为何放弃、当前定论」，供联调与答辩引用。

---

## 1. 目标与约束

- 40+ 把拍柄 **非连接** 同时上报，主控 SS928 扫描解析
- IMU **10Hz**（100ms），MPU-9250 软件 I2C
- 批量部署：MAC `cc:ad:c9:00:22:01`～`08` 独立 `*_all.fwpkg`

---

## 2. 里程碑

| 阶段 | 内容 | 结果 |
|------|------|------|
| 1 | MPU9250 + UART 调试 | 采样与 Mahony 可用 |
| 2 | BLE GATT Notify ASCII | 连接调试通路稳定 |
| 3 | SLE 非连接扫播 + 22B `EB 1A 02` | 多设备架构成立 |
| 4 | 联调：停播换帧、热更新、ASCII 往返 | 见下表 |
| 5 | 远距离优化：回二进制 + 合法间隔 | **当前量产** |
| 6 | GitHub 方案 C + Skill + 主控文档 | 工程化 |

---

## 3. 联调踩坑表（核心）

| 现象 | 根因 | 最终处理 |
|------|------|----------|
| BurnTool 超时 | 烧 `fota.fwpkg`（OTA 格式） | 仅 `*_all.fwpkg` |
| 扫到名无 IMU | 数据只在 ADV，名在 ScanRsp | **ADV+ScanRsp 双份 0xFF** |
| 包头对、数值全 0 / uptime 不变 | 播着时 `set_announce_data` 不刷新空口 | **100ms async stop→disable→start** |
| `restart fail 0x8000600a` | 同步 stop 后立刻 start | 只在 **disable 回调**里 commit+start |
| 180ms 限流 restart | 同一帧重复过久 | 去掉限流，每 100ms 必须换帧 |
| 5m ~80% 正确，7m 差 | ASCII ~50B 空口长 + CRC 弱 | **改回 22B 二进制** |
| 间隔 0x28 仍不稳 | 可能低于栈最小间隔 | **0x50（10ms）** |
| 功率「设 20 无效」 | 未 `sle_customize_max_pwr` | `sle_customize_max_pwr(8,2)` |
| RSSI「80 多」 | 误读为正数 | 文档：-80 dBm |
| 二进制 + 主控 ASCII | 字段错位 A+980 | 主控改二进制解析 |

---

## 4. 载荷格式演进

```
22B 二进制 ──► ASCII 行（主控 sscanf 方便）──► 22B 二进制（7m+ 优化）
     ↑                                              ↑
  初期联调                                    2026-07 当前
```

**ASCII 示例**（仅 BLE Notify / 已弃用扫播）：
```
@1281,A+30,+980,+50,G+1,+2,R+0,P+0,M100\n
```

**二进制**（星闪扫播当前）：见 `主控对接说明-远距离二进制版.md`

---

## 5. 广播刷新状态机（当前）

```
push_sensor (100ms)
  → stage_announce_buffers (RAM 组 ADV+ScanRsp)
  → rf_publish: sle_stop_announce
  → announce_disable_cbk: commit + sle_start_announce
```

**不可**在主循环同步 `stop→sleep→start`；**不可**省略 disable 回调路径。

---

## 6. 文件职责

| 文件 | 职责 |
|------|------|
| `common/paibing_imu.c` | 100ms 循环、校准、watchdog |
| `sle/paibing_sle_server_adv.c` | 广播参数、22B 组包、async restart |
| `sle/paibing_sle_server.c` | `sle_customize_max_pwr` |
| `sle/mac_config.h` | MAC 末字节、扫播开关 |
| `tools/build_paibing_sle.sh` | 改 MAC + 编译 + 复制 all.fwpkg |

---

## 7. 硬件

| 功能 | 引脚 |
|------|------|
| UART | MGPIO19/20 |
| I2C | MGPIO16 SCL / MGPIO15 SDA |
| 复位键 | MGPIO22 |

---

## 8. 答辩可强调的设计点

1. **transport 抽象**：IMU 与 SLE/BLE 解耦，一套采样两路无线
2. **非连接扫播**：突破连接数，适配 40+ 设备
3. **载荷与刷新分离**：stage（内存） vs rf_publish（空口），适配协议栈异步语义
4. **双份 ScanRsp**：补偿远距离 ADV 丢失
5. **二进制 vs ASCII 权衡**：联调便利 vs 空口效率，按距离需求选型
