---
name: paibing-project
description: >-
  Full-stack paibing smart badminton training system: BS20 racket IMU firmware
  + SS928 SLE scan, dedup, swing/CNN hit detection, Qt UI. Use for end-to-end
  protocol changes, cross-team integration, GitHub repo docs, or debugging
  IMU data from broadcast to UI hit event.
---

# 星羽汇聚 · 拍柄 + 主控全栈 Skill

## 系统一览

| 端 | 芯片 | 目录 | 职责 |
|----|------|------|------|
| 拍柄 | BS20-N1200 | `bs20/` | MPU9250、星闪 22B 二进制广播 |
| 主控 | SS928 + WS73 | `ss928/` + 内部 `version3.0/` | 扫描解析、挥拍/CNN、Qt UI |

**当前协议（2026-07）**：星闪 **22B `EB 1A 02`**，ADV+ScanRsp 双份 0xFF，主控 **MAC+uptime 去重**，有效 **10Hz**。

> BLE Notify 仍为 ASCII `@` 行。

## 子 Skill 分工

| Skill | 何时用 |
|-------|--------|
| [paibing-bs20](paibing-bs20/SKILL.md) | 改拍柄固件、编译 fwpkg、广播策略 |
| [paibing-ss928](paibing-ss928/SKILL.md) | 改主控解析、去重、击球检测、部署 |

## 开发历程摘要（答辩用）

### 阶段 1：拍柄 IMU 基础
- MPU-9250 软件 I2C + Mahony AHRS + 100ms 主循环
- UART 调试、陀螺零偏校准

### 阶段 2：无线上报
- BLE：连接后 GATT Notify ASCII 行（调试通路）
- SLE：非连接扫播，设备名 `paibing_imu`，MAC `cc:ad:c9:00:22:xx`

### 阶段 3：协议试错与定型
- 试过 ADV 长 ASCII → 远距离丢包
- 22B `EB 1A 02` 二进制 → 7m+ 收包好
- 短暂回 ASCII → 7m+ 收包率下降
- **当前（499a3735）**：再改回 **22B 二进制**，主控二进制优先解析

### 阶段 4：主控星闪接收
- `sle_seek_print_all`：TLV 解 0xFF，**EB 1A 优先**，转 `@` 行
- `sle_imu_bridge.sh` → `/tmp/sle_imu_lines`
- **MAC + @uptime_ms 去重**（C 层 + Qt 双层）

### 阶段 5：端侧智能与 UI
- Qt 训练面板：单人练习 / 班级 / 对打
- 1D CNN（`badminton_npu.om`）六类挥拍分类
- 规则 FSM + CNN `both` 模式击球
- 摄像头 YOLO + IMU 多源 OR 触发

### 阶段 6：联调优化
- 击球灵敏度调钝（dyn/gyro/confirm/cooldown）
- 对打模式多拍柄布局、IMU 触发
- GitHub 仓文档化 + `ss928/reference/` 参考代码

## 端到端 Checklist

协议变更时必须双端同步：

- [ ] 拍柄 `paibing_sle_server_adv.c` 载荷格式
- [ ] 主控 `sle_seek_print_client.c` 解析顺序
- [ ] 主控去重逻辑
- [ ] `imu_swing_detector.cpp` 字段单位
- [ ] `bs20/docs/主控对接说明.md` + `固件变更记录.md`
- [ ] `ss928/reference/` 快照更新

## 关键文档

| 文档 | 读者 |
|------|------|
| [README.md](../../README.md) | 全员入口 |
| [docs/开发过程与代码要点.md](../../docs/开发过程与代码要点.md) | 答辩/交接 |
| [docs/联调快速指南.md](../../docs/联调快速指南.md) | 联调 |
| [bs20/docs/主控对接说明.md](../../bs20/docs/主控对接说明.md) | 协议权威 |

## GitHub 仓库

https://github.com/liangchengkongxu/xingyuhuiju2026qiansaizuopinSLE-AI-IMU

注意仓库名含 **SLE**（不是 `...-AI-IMU`）。
