# HTTP 上报接口

Base URL：`http://47.107.120.9/api/v1`

---

## 1. 上报比赛（对打）

**POST** `/device/ingest/match`

### 请求体

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `device_id` | string | 是 | 设备唯一标识，如 `xingyu-hi3403-01` |
| `user_phone` | string | **强烈建议** | App 注册用户手机号，如 `13800138000` |
| `title` | string | 是 | 比赛标题 |
| `opponent_label` | string | 否 | 对手描述，默认 `对打伙伴` |
| `duration_min` | int | 否 | 时长（分钟） |
| `strokes` | array | 是 | 逐拍列表，见下表 |

**strokes[] 每项：**

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `action_type` 或 `action_type_label` | string | 是 | 动作中文名，如 `杀球` |
| `score` | int | 是 | 得分 0–100 |
| `ai_suggestion` | string | 否 | AI 建议文案 |
| `ball_speed_kmh` | int | 否 | 球速 km/h |
| `power_n` | int | 否 | 力度（牛顿或相对值，App 原样展示） |

### 响应

```json
{ "ok": true, "matchId": "42" }
```

### 后端行为

- 按 `user_phone` 查找用户；找不到则落到第一个注册用户（**联调时务必填对手机号**）
- 自动计算 `stroke_count`、`avg_score`

---

## 2. 上报练习（单人动作）

**POST** `/device/ingest/drill`

### 请求体

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `device_id` | string | 是 | 设备 ID |
| `user_phone` | string | **强烈建议** | 用户手机号 |
| `action_type` | string | 是 | 见下表枚举 key |
| `score` | int | 是 | 得分 |
| `ai_suggestion` | string | 否 | AI 建议 |
| `ball_speed_kmh` | int | 否 | 球速 |
| `power_n` | int | 否 | 力度 |

### action_type 枚举

| key | 含义 |
|-----|------|
| `net_drop` | 放网 |
| `smash` | 杀球 |
| `clear` | 高远 |
| `lift` | 挑球 |
| `drive` | 平抽 |

### 响应

```json
{ "ok": true, "drillId": "128" }
```

---

## 3. 错误

| HTTP | detail | 原因 |
|------|--------|------|
| 400 | 请先注册至少一个用户 | 服务器无用户，需 App 先注册 |
| 422 | Validation Error | JSON 字段缺失或类型错误 |

---

## 4. 完整样例

见同目录 `examples/match_sample.json`、`examples/drill_sample.json`。

---

## 5. 与 App 字段对应

App 展示字段与上报字段一致：

| App 界面 | 上报字段 |
|----------|----------|
| 均分 | strokes[].score 平均值（后端算） |
| 球速 | ball_speed_kmh |
| 力度 | power_n |
| AI 建议 | ai_suggestion |
| 动作类型 | action_type / action_type_label |
