# 骨骼与单人击球回放

代码主文件：

| 模块 | 路径 |
|------|------|
| Pose 推理 + 回放采集/导出 | `version3.0/ai/sample_vio_ai.c` |
| VPSS ch3 回放通道 | `version3.0/src/camera_pipe.c` |
| 训练/总结/回放 UI | `version3.0/src/pages_training.cpp` |
| 回放路径与 session IPC | `version3.0/src/ui_common.cpp` |
| 页面路由（勿过早清 session） | `version3.0/src/main_window.cpp` |
| PPM 帧序列播放器 | `version3.0/src/ui_pages.h`（`FrameReplayWidget`） |

---

## 骨骼：实时 vs 回放（2026-07 当前方案）

### 数据流

```
VPSS ch1/ch2 640×640 → best_pose_aipp.om（NPU）
  → pose_decode + 稳定化
  → g_pose_result（供回放 stamp 使用）

实时预览（默认关）:
  WIDGET_POSE_RGN=0 → 不在 ch0 画 RGN 线

击球回放:
  VPSS ch3 960×540 → 每帧保存 pose 快照
  → 导出时 pose_stamp_on_replay_nv12() 画线 → PPM
```

- **实时预览默认无骨骼**（`WIDGET_POSE_RGN=0`），减轻 ch0 RGN 与 VO 压力。
- **回放骨骼**由软件写入 NV12 Y 平面，再 BT.601 limited 转 RGB PPM。
- 恢复实时骨骼：设 `WIDGET_POSE_RGN=1` 并重启。

### Pose 调参（`run.sh`）

| 变量 | 默认 | 说明 |
|------|------|------|
| `WIDGET_POSE_ENABLE` | `1` | 开启 Pose 推理（回放 stamp 依赖） |
| `WIDGET_POSE_RGN` | `0` | 实时 RGN 骨骼（0=关） |
| `WIDGET_POSE_CONF` | `0.10` | 人体框置信度 |
| `WIDGET_POSE_KPT_VIS` | `0.25` | 关键点可见阈值 |
| `WIDGET_POSE_INTERVAL` | `2` | 每 N 帧推理一次 |
| `WIDGET_POSE_HOLD_MS` | `120` | 丢失检测后保留 ms |
| `WIDGET_POSE_CH1_ONLY` | `1` | attach 模式在 ch1 帧上跑 pose |

### 监控

```bash
tail -f /tmp/sample_vio_ai.log | grep -E 'infer fps|ai_pose|hit_replay'
# 期望: ai_pose: live RGN off, replay stamp only
```

---

## 单人击球回放

### 链路

```
TrainingPage 击球
  → requestHitReplayCapture → /tmp/.widget_replay_req
  → publishReplaySession   → /tmp/.widget_replay_session

sample_vio_ai（VPSS ch3 worker @ 20fps）:
  环形缓冲 pre 2s + post 2s
  → hit_idx <= 3: 立即导出 PPM（带骨骼）
  → hit_idx > 3:  仅存 raw/*.nv12 + raw/*.pose（meta stage=raw）
  → 用户点进详情 → /tmp/.widget_replay_pose_req → 后台渲染 PPM

ActionDetailPage:
  resolveReplayClip() 轮询 → playReplayClip（MP4 或 PPM 目录）
```

板端输出：`/opt/widget_ui/replays/{sessionId}/hit_{N}/`

### 当前参数

| 项 | 值 |
|----|-----|
| 采集源 | **VPSS ch3** 960×540 16:9（不 bind VO） |
| 输出 | **960×540** @ **20fps**，约 80 帧/段（前后各 2s） |
| 颜色 | BT.601 limited-range NV12→RGB |
| 骨骼 | 采集时存 pose 快照；导出/stamp 时画线 |
| 按需渲染 | 前 **3** 条 eager；第 4 条起点进详情再渲染 |

### 关键环境变量

| 变量 | 默认 | 说明 |
|------|------|------|
| `WIDGET_REPLAY_LIVE` | `1` | 击球前环形缓冲 pre 帧 |
| `WIDGET_REPLAY_FPS` | `20` | 回放采集/导出帧率 |
| `WIDGET_REPLAY_VPSS_CHN` | `3` | **专用回放通道**；**勿设为 0**（抢 ch0 预览） |
| `WIDGET_REPLAY_POSE_EAGER_MAX` | `3` | 立即导出骨骼的回放条数 |
| `WIDGET_REPLAY_SRC_NV21` | `1` | ch3 UV 顺序 |
| `WIDGET_REPLAY_DIR` | `/opt/widget_ui/replays` | 回放根目录 |

### UI 行为

- 离开训练页进入总结页时 **不能** `clearReplaySession()`。
- 第 1–3 次击球：总结页点进详情应立即可播。
- 第 4 次及以后：详情页显示「正在生成击球回放（骨骼渲染中）…」，完成后自动播放。
- `tryStartReplay()`：约 90s 超时；`FrameReplayWidget` 从 `meta.txt` 读 fps。

---

## 已知坑（必读）

| 现象 | 原因 | 处理 |
|------|------|------|
| 预览卡死 / 进程崩溃 | 从 **ch0** 抢帧做回放 | `WIDGET_REPLAY_VPSS_CHN=3`，reboot |
| 总结页点进卡顿 | 每条回放同步做 RGB+骨骼 | 已 lazy：第 4 条起按需渲染 |
| 第 4 条起一直「生成中」 | raw 已存但未触发 pose 渲染 | 点进详情会自动请求；查 pose_req 日志 |
| 回放无骨骼 | `WIDGET_POSE_ENABLE=0` 或 pose 未检出 | 保持 Pose 开启；看 log |
| `vb_set_conf failed` | MPP 未释放 | **reboot** 后再 `bash run.sh` |
| 板端无 ffmpeg | 无 MP4 | `FrameReplayWidget` 播 PPM |

---

## 部署

```bash
# PC
bash version3.0/scripts/build_vio_ai.sh
bash version3.0/scripts/deploy_bin.sh
bash version3.0/scripts/deploy_panel.sh ui_common.cpp pages_training.cpp

# 板端
cd /opt/widget_ui && bash run.sh
```

验证：

```bash
grep hit_replay /tmp/sample_vio_ai.log | tail -5
# 期望: pose_eager=3 out=960x540 src_chn=3
grep ai_pose /tmp/sample_vio_ai.log | tail -1
# 期望: live RGN off, replay stamp only

cat /opt/widget_ui/replays/<session>/hit_1/meta.txt
# pose=1 stage=done
cat /opt/widget_ui/replays/<session>/hit_4/meta.txt
# pose=0 stage=raw  （点进详情后变为 pose=1 stage=done）
```
