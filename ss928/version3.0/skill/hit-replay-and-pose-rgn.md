# 骨骼 RGN 叠加与单人击球回放

代码主文件：

| 模块 | 路径 |
|------|------|
| Pose 推理 + RGN + 回放采集 | `version3.0/ai/sample_vio_ai.c` |
| 训练/总结/回放 UI | `version3.0/src/pages_training.cpp` |
| 回放路径与 session IPC | `version3.0/src/ui_common.cpp` |
| 页面路由（勿过早清 session） | `version3.0/src/main_window.cpp` |
| PPM 帧序列播放器 | `version3.0/src/ui_pages.h`（`FrameReplayWidget`） |

---

## 骨骼（Pose）RGN

### 数据流

```
VPSS ch2 640×640 → best_pose_aipp.om（NPU）
  → pose_decode + 稳定化（snap / 滞后可见性）
  → ss_mpi_rgn LINE 叠加到 VPSS ch0 预览（3840×2160）
```

- 骨骼**仅画在 RGN 硬件层**，不写入 NV12 帧本身。
- 坐标从 640 检测空间映射到 `g_preview_src_w/h`（通常 3840×2160）。
- 海思 RGN 线宽上限 **8**；4K 预览时自适应线宽。

### 关键环境变量（`run.sh`）

| 变量 | 默认 | 说明 |
|------|------|------|
| `WIDGET_POSE_CONF` | `0.10` | 人体框置信度 |
| `WIDGET_POSE_KPT_VIS` | `0.25` | 关键点可见阈值 |
| `WIDGET_POSE_INTERVAL` | `2` | 每 N 帧推理一次（骨骼约 infer_fps/N） |
| `WIDGET_POSE_HOLD_MS` | `120` | 丢失检测后保留骨架 ms |
| `WIDGET_POSE_MISS_MAX` | `3` | 连续 N 次失败才清骨架 |
| `WIDGET_POSE_STABLE_PX` | `12` | 静止区判定 |
| `WIDGET_POSE_KPT_SNAP_PX` | `8` | 小位移 snap，减静止抖动 |
| `WIDGET_POSE_BBOX_JUMP_PX` | `28` | 仅 bbox 大跳变时 hard_reset |
| `WIDGET_POSE_CH1_ONLY` | `1` | attach 模式在 ch1 帧上跑 pose |

### 监控

```bash
tail -f /tmp/sample_vio_ai.log | grep -E 'infer fps|pose rgn'
cat /proc/umap/npudev    # NPU 占用（无 npu-smi）
```

骨骼推理帧率 ≈ `infer fps / WIDGET_POSE_INTERVAL`。

---

## 单人击球回放

### 链路

```
TrainingPage 击球
  → requestHitReplayCapture → /tmp/.widget_replay_req
  → publishReplaySession   → /tmp/.widget_replay_session

sample_vio_ai:
  hit_replay_poll_trigger → 环形缓冲 + post 采集
  → PPM 帧 + meta.txt + done.flag（可选 ffmpeg MP4）

ActionDetailPage:
  resolveReplayClip() 轮询 → playReplayClip（MP4 或 PPM 目录）
```

板端输出目录：`/opt/widget_ui/replays/{sessionId}/hit_{N}/`

### 当前参数（2026-07）

| 项 | 值 |
|----|-----|
| 采集源 | **VPSS ch1**（640×640，与 AI attach 同通道） |
| 输出 | **960×540** NV12 → PPM RGB |
| 缩放 | 比例不一致时 **letterbox**；一致时 bilinear |
| 前后缓冲 | 各 2s × `WIDGET_REPLAY_FPS`（默认 10fps → 40 帧） |
| 骨骼写入回放 | `pose_stamp_on_replay_nv12()` 软件画线到 Y 平面 |
| 异步 worker | live ring 模式下 copy 640 帧到 worker 线程缩放，不阻塞 infer |

### 关键环境变量

| 变量 | 默认 | 说明 |
|------|------|------|
| `WIDGET_REPLAY_LIVE` | `1` | 1=击球前也环形缓冲 pre 帧 |
| `WIDGET_REPLAY_FPS` | `10` | 回放采集/导出帧率 |
| `WIDGET_REPLAY_VPSS_CHN` | **`-1`** | **勿设为 0**：ch0 与 RGN/VO 共用，抢帧会导致骨骼消失、进程崩溃 |
| `WIDGET_REPLAY_SRC_NV21` | 未设置 | 强制 UV 顺序：`-1` 自动，`0`=NV12，`1`=NV21 |
| `WIDGET_REPLAY_DIR` | `/opt/widget_ui/replays` | 回放根目录 |

### UI 注意

- 离开训练页进入总结页时 **不能** `clearReplaySession()`，否则 post 采集饿死 → 永久「正在生成」。
- 仅在 `goHome()` 时清除 session。
- `ActionDetailPage::tryStartReplay()`：90s 超时提示；`FrameReplayWidget` 从 `meta.txt` 读 fps。

---

## 已知坑（必读）

| 现象 | 原因 | 处理 |
|------|------|------|
| 骨骼完全消失 | 从 **ch0** 高频 `get_chn_frame` 抢预览帧 | `WIDGET_REPLAY_VPSS_CHN=-1`，reboot 清 MPP |
| 第 3 次起回放一直「生成中」 | 离开 page4 时清 session，post 帧断供 | 已改：仅 goHome 清 session |
| 回放比例/颜色怪 | 640 方图硬拉伸到 16:9；UV 顺序错 | letterbox + NV12 转换；勿用 ch0 抢帧 |
| `sample_vio_ai` 反复 defunct | ch0 抢帧 + 3840 缩放过重 | 同上 + reboot |
| `vb_set_conf failed` | MPP 未释放 | **reboot** 后再 `bash run.sh` |
| 板端无 ffmpeg | 无 MP4，走 PPM 目录播放 | `FrameReplayWidget` 备用 |

---

## 部署

```bash
# PC
bash version3.0/scripts/build_vio_ai.sh
bash version3.0/scripts/deploy_bin.sh          # sample_vio_ai + run.sh
bash version3.0/scripts/deploy_panel.sh        # 或指定 cpp：main_window pages_training ui_pages

# 板端（改 AI 后需完整重启，勿只 kill sample_vio_ai 若 MPP 已乱）
cd /opt/widget_ui && bash run.sh
# 或 reboot
```

验证新回放（须重新练习，旧 session 仍是旧分辨率）：

```bash
grep hit_replay /tmp/sample_vio_ai.log | tail -3
# 期望: out=960x540 src_chn=-1
ls /opt/widget_ui/replays/*/*/meta.txt
cat /opt/widget_ui/replays/<session>/hit_1/meta.txt
```
