# 星羽汇聚 · 板端对接交接包

> 给 **Hi3403 / L610 板端** 同事。App 与云端业务后端已就绪，板端只需按本文档 HTTP 上报训练数据。

## 你需要做什么

1. 端侧识别出每一拍/每一次练习的结构化结果
2. 通过 4G（L610）或局域网 **POST JSON** 到阿里云后端
3. 每条数据带上 **`user_phone`**（用户在 App 注册的手机号），数据才会出现在该用户（及教练班级）界面

## 上报地址（生产）

| 场景 | URL |
|------|-----|
| 对打/比赛整场 | `http://47.107.120.9/api/v1/device/ingest/match` |
| 单人动作练习单次 | `http://47.107.120.9/api/v1/device/ingest/drill` |

- Content-Type：`application/json`
- **目前无需 Token**（联调方便；上线前可能加 device 鉴权）
- 在线文档：`http://47.107.120.9/docs`（搜 `device/ingest`）

## 本文件夹内容

| 文件 | 说明 |
|------|------|
| [架构与数据流.md](./架构与数据流.md) | 系统分工、数据怎么走 |
| [HTTP上报接口.md](./HTTP上报接口.md) | 字段定义、响应、错误 |
| [ThingsKit-MQTT参考.md](./ThingsKit-MQTT参考.md) | 实验室 MQTT 可选方案（App 不读） |
| [联调示例.md](./联调示例.md) | curl / 脚本测试 |
| [examples/match_sample.json](./examples/match_sample.json) | 比赛上报样例 |
| [examples/drill_sample.json](./examples/drill_sample.json) | 练习上报样例 |

## 联调前准备

1. 确认服务器可达：`curl http://47.107.120.9/health` → `{"status":"healthy"}`
2. 测试用户先在 App 注册，或使用演示号 **`13800138000` / `123456`**
3. 上报时 `user_phone` 填该手机号
4. 打开 App → 首页 → 对打/练习，或教练端班级 → 学员详情，应能看到新数据

## 练习动作类型（`action_type`）

| key | 中文 |
|-----|------|
| `net_drop` | 放网 |
| `smash` | 杀球 |
| `clear` | 高远 |
| `lift` | 挑球 |
| `drive` | 平抽 |

## 对打逐拍动作名（`action_type` / `action_type_label`）

中文即可，如：`杀球` `高远` `平抽` `放网` `挑球` `吊球` `封网`

## 联系人 / 问题反馈

- App + 云端 API：全栈/App 同事
- 板端识别算法 + 上报实现：你
- 联调问题优先查 [联调示例.md](./联调示例.md)，确认 `user_phone` 与 JSON 字段

## 版本

- 交接包日期：2026-06
- 后端版本：FastAPI @ 47.107.120.9
- App 版本：星羽汇聚 v0.10
