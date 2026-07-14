# 星羽汇聚后端部署说明

## 服务器信息

- 公网 IP：`47.107.120.9`
- 推荐开放端口：22、80、443、8000

## 一键部署（Workbench 终端）

### 1. 安全组

阿里云 ECS → 安全组 → 入方向添加 **TCP 80**、**TCP 8000**（来源 0.0.0.0/0）

### 2. 上传代码

把整个 `backend` 文件夹上传到服务器 `/opt/xingyu-backend`

可用 Workbench「文件」上传，或本机 PowerShell：

```powershell
scp -r D:\smallapp\backend root@47.107.120.9:/opt/xingyu-backend
```

### 3. 服务器执行

```bash
cd /opt/xingyu-backend
chmod +x scripts/deploy.sh
bash scripts/deploy.sh
curl http://127.0.0.1/health
curl http://47.107.120.9/health
```

### 4. 验证 API

```bash
curl -X POST http://47.107.120.9/api/v1/auth/register \
  -H "Content-Type: application/json" \
  -d '{"displayName":"测试","phone":"13800000001","password":"123456"}'
```

## App 配置

`baseUrl` 设为：`http://47.107.120.9/api/v1/`

## 主要接口

| 接口 | 说明 |
|------|------|
| POST /api/v1/auth/register | 注册 |
| POST /api/v1/auth/login | 登录 |
| GET /api/v1/matches | 比赛列表 |
| GET /api/v1/drills?action=smash | 练习记录 |
| GET /api/v1/community/posts | 动态 |
| POST /api/v1/device/ingest/match | 板端上报比赛 |

文档：`http://47.107.120.9/docs`
