# 星羽汇聚 · 2026 嵌赛海思 SS928 端侧智能作品

一套基于 **星闪（SLE）+ 端侧智能** 的智能羽毛球训练系统。

| 模块 | 目录 | 说明 |
|------|------|------|
| **BS20 拍柄端** | [`bs20/`](bs20/) | MPU-9250 九轴采集，星闪/蓝牙无连接或连接上报 |
| **SS928 主控端** | [`ss928/`](ss928/) | 端侧智能、SLE 扫描解析、训练业务（待上传） |

---

## 系统架构

```
  ┌─────────────┐    星闪广播 / BLE      ┌──────────────┐
  │  BS20 拍柄   │  ──────────────────►  │  SS928 主控   │
  │  MPU-9250   │   EB 1A 02 九轴帧     │  端侧 AI      │
  └─────────────┘                       └──────────────┘
```

- **拍柄**：100 ms 采样，Mahony 姿态解算，非连接扫播（支持 40+ 设备）
- **主控**：扫描 `paibing_imu`，解析厂商域 `0xFF` 中 22 字节二进制帧

---

## 快速导航

| 文档 | 说明 |
|------|------|
| [**bs20/docs/主控对接说明-远距离二进制版.md**](bs20/docs/主控对接说明-远距离二进制版.md) | **主控必读**：22B 二进制解析、去重、示例代码 |
| [**bs20/docs/主控对接说明.md**](bs20/docs/主控对接说明.md) | 主控对接总览 |
| [**ss928/docs/主控待办清单.md**](ss928/docs/主控待办清单.md) | 主控同事 Checklist |
| [bs20/docs/固件变更记录.md](bs20/docs/固件变更记录.md) | 最近拍柄固件改动摘要 |
| [docs/开发过程与代码要点.md](docs/开发过程与代码要点.md) | 开发历程与踩坑 |
| [.cursor/skills/paibing-bs20/SKILL.md](.cursor/skills/paibing-bs20/SKILL.md) | Cursor Agent Skill |

### BS20 拍柄固件

```bash
# 需先安装 HiSilicon BS2X SDK，再按 bs20/integration 说明集成
cd bs20
# 见 bs20/README.md
```

### SS928 主控（即将添加）

代码将放在 [`ss928/`](ss928/) 目录。

---

## 仓库结构

```
.
├── README.md           # 本文件（项目总览）
├── docs/               # 项目级文档
│   └── 开发过程与代码要点.md
├── .cursor/skills/     # Cursor Agent Skill
│   └── paibing-bs20/
├── bs20/               # BS20-N1200 拍柄固件
│   ├── application/    # paibing 应用源码
│   ├── tools/          # 构建脚本
│   ├── docs/           # 协议与代码索引
│   └── integration/    # SDK 集成 overlay
└── ss928/              # SS928 端侧智能（待补充）
```

---

## 数据协议（星闪广播 · 当前）

**22 字节二进制** `EB 1A 02`，放在 ADV 与 Scan Response 的 **0xFF 厂商域**：

```
EB 1A 02 DEV  uptime(2)  ax ay az  gx gy  roll pitch  mag(2)
```

- 100ms 换帧；主控须按 **uptime（偏移 4～5）+ MAC 去重**（10ms 广播会重复扫到同一帧）
- 详见 [**bs20/docs/主控对接说明-远距离二进制版.md**](bs20/docs/主控对接说明-远距离二进制版.md)

---

## 依赖与许可

- BS20 固件基于 [HiSilicon BS2X SDK](https://gitee.com/HiSpark/fbb_bs2x) 开发，需自行下载 SDK 后集成本仓库 `bs20/` 内容。
- 应用层新增代码 Copyright (c) 2026 星羽汇聚参赛队。

---

## 团队

2026 全国大学生嵌入式芯片与系统设计竞赛 · 海思赛题 · SS928 端侧智能
