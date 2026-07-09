# Cursor Agent Skills

本目录供 **Cursor Agent** 在二次开发、联调排障时快速恢复上下文。人类读者也可作速查。

## 使用方式

在 Cursor 中开发本仓库或关联的 `version3.0/` 工程时，Agent 会根据 description 自动匹配 Skill。也可在对话中 `@` 引用对应 SKILL.md。

## Skill 列表

| Skill | 文件 | 适用场景 |
|-------|------|----------|
| **paibing-project** | [paibing-project/SKILL.md](paibing-project/SKILL.md) | 全栈、协议变更、答辩级上下文 |
| **paibing-bs20** | [paibing-bs20/SKILL.md](paibing-bs20/SKILL.md) | 拍柄固件、编译烧录、广播策略 |
| **paibing-ss928** | [paibing-ss928/SKILL.md](paibing-ss928/SKILL.md) | 主控解析、去重、击球检测、部署 |

## 阅读顺序（新会话）

1. `paibing-project` — 了解全栈与当前协议
2. 按任务选 `paibing-bs20` 或 `paibing-ss928`
3. 详文见 [docs/开发过程与代码要点.md](../docs/开发过程与代码要点.md)

## 与 version3.0/skill 的关系

队伍内部完整 Qt 工程另有 `version3.0/skill/`（含 YOLO、自启、单人练习 UI 等专题）。  
本仓库 Skill 聚焦 **拍柄↔主控 IMU 链路**；视觉/NPU attach 细节见内部工程。
