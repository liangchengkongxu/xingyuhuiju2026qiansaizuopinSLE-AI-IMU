# ThingsKit MQTT 参考（可选）

> **重要**：Android App **只读 FastAPI**，不消费 ThingsKit 数据。  
> 若实验室要求板端同时上 ThingsKit，可与 HTTP 上报 **并行**，但 **业务以 HTTP 为准**。

## 连接参数

| 项 | 值 |
|----|-----|
| 控制台 | https://thingskit.aiotcomm.com.cn/login |
| MQTT Broker | `thingskit.aiotcomm.com.cn` |
| 端口 | `11883` |
| 主题 | `v1/devices/me/telemetry` |
| 用户名 | 设备 AccessToken |
| 密码 | 项目 ProjectKey |

## 遥测 JSON 建议

与 HTTP 字段对齐，便于对照：

```json
{
  "mode": "drill",
  "action_type": "smash",
  "score": 91,
  "ball_speed_kmh": 168,
  "power_n": 72,
  "ai_suggestion": "击球点再靠前",
  "user_phone": "13800138000",
  "device_id": "xingyu-hi3403-01"
}
```

对打模式可发：

```json
{
  "mode": "match_stroke",
  "action_type": "杀球",
  "score": 88,
  "ball_speed_kmh": 156,
  "power_n": 92,
  "ai_suggestion": "提前引拍",
  "stroke_seq": 3,
  "match_id": "local-session-001"
}
```

## 物模型标识符建议

`mode`, `action_type`, `score`, `ball_speed_kmh`, `power_n`, `ai_suggestion`, `user_phone`, `device_id`, `stroke_seq`, `match_id`

## 命名规范

共用租户内，设备名建议加前缀 **`xingyu-`**，避免与他人设备冲突。

## 管理 API（创建设备等）

```
POST https://thingskit.aiotcomm.com.cn/api/auth/login
Body: {"username":"...", "password":"..."}
→ Bearer token

GET /api/tenant/devices?page=0&pageSize=50
```

账号由实验室管理员分配，**不要**写进 Git 或交接包明文。

## 何时用 HTTP、何时用 MQTT

| 通路 | 用途 |
|------|------|
| **HTTP → FastAPI** | **必须**。App、教练端、排行都读这里 |
| MQTT → ThingsKit | 可选。实验室监控、大屏、调试 |
