# 联调操作记录（Agent 会话摘要）

> 记录 SS928 Widget v3.0 在本项目中的主要集成与 UI 联调，便于后续 Agent 延续上下文。  
> 板端默认：`root@192.168.1.168`，密码 `ebaina`，安装目录 `/opt/widget_ui`。

---

## 1. IMU 1D CNN 班级 / 单人击球

### 完成内容

- 集成 `badminton_npu.om`（8 通道 × 24 帧 → 6 类：高远/平抽/挑球/放网/发球/杀球）
- **修复 ch5**：广播无 Gz，ch5 改为 **pitch（°）**，与 `badminton_preprocess.h` 一致
- `WIDGET_IMU_HIT_MODE=both`：CNN 优先，规则 FSM 补漏（放网/挑球）
- 软触发环境变量：`WIDGET_IMU_CNN_SOFT_*`
- 日志区分：`hit(cnn)` vs `hit(rule)`，可选 `WIDGET_IMU_CNN_DEBUG=1`

### 关键文件

- `src/imu_cnn_classifier.cpp`、`src/imu_swing_detector.cpp`、`src/sle_imu_service.cpp`
- `scripts/import_deploy_pack_imucnn.sh`、`scripts/build_imu_cnn.sh`
- 主文档章节：[`../README.md`](../README.md)「班级 IMU 击球检测」

---

## 2. 开机自启与快速启动（~30s → ~8s）

### 问题

- 原 `sleep 10` + 等 `multi-user.target` + snap 拖尾，约半分钟才见界面

### 改动

| 项 | 做法 |
|----|------|
| systemd | `WantedBy=rc-local.service`；`After=rc-local`；轮询 `/dev/fb0` 替代 sleep 10 |
| 环境 | `WIDGET_BOOT_FAST=1`、`WIDGET_CAM_ISP_CLEAN=0`（冷启动） |
| run.sh | fast 模式跳过 sleep 1 / 重型 ws73 teardown，仍 kill 残留 camera 进程 |
| vo_gfbg_init.c | 缩短 AI attach 前 usleep（350ms / 120ms） |
| 冲突服务 | disable `kiosk.service`、`ui-server.service` |

### 安装注意

- **第一次 unit 安装失败**：scp 上传的 unit 变成全 **null 字节** → `bad-setting`
- **修复**：`install_autostart.sh` 用 **ssh 直写** unit 文件

### 验证

```bash
systemd-analyze critical-chain widget_ui.service
tail -f /tmp/widget_ui_boot.log
# 冷启动：rc-local 后约数秒内 widget_panel 起来
```

详见 [`boot-autostart-and-fast-start.md`](boot-autostart-and-fast-start.md)。

---

## 3. 单人练习 — 击球触发（三路 OR）

### 需求

图像高置信 **或** 拍柄大幅波动 **或** CNN 九轴匹配，满足任一即计数；类型仍固定为当前练习动作。

### 实现（`TrainingPage` + `run.sh`）

| 路径 | 条件（默认） |
|------|----------------|
| 摄像头稳定 | `WIDGET_PRACTICE_CAM_STABLE_PERCENT=52` |
| 摄像头挥拍 | `WIDGET_YOLO_SWING=1`，`WIDGET_PRACTICE_CAM_SWING_PERCENT=48`，`WIDGET_YOLO_SWING_FIRE_PERCENT=48` |
| CNN | `WIDGET_IMU_CNN_TRIGGER_CONF=0.26` |
| 规则大幅波动 | `WIDGET_IMU_BURST_MIN_DYN=8`、`WIDGET_IMU_BURST_MIN_GYRO=58` |
| 去重 | `WIDGET_PRACTICE_HIT_COOLDOWN_MS=950` |

- `WIDGET_HIT_SOURCE=both`（摄像头 + 拍柄）
- `subscribeImu()`：单人练习同时订阅 camera + IMU

详见 [`imu-cnn-and-hit-detection.md`](imu-cnn-and-hit-detection.md)。

---

## 4. 单人练习 — SkillDetailPage UI

### 迭代摘要

1. 去掉左侧 Sidebar（训练总览/连接设备等）
2. 左侧改为 **动作要领** 区（宽 448px）
3. 右侧：教学视频 + 绿色大按钮「开始练习」
4. 视频约 **1280×720**，按钮宽 **1160px**，按钮下移（margin-top 40px）
5. 右上角：**返回上级=绿色实心**，**返回首页=红色实心**
6. 五个动作要领精简文案 + **大字号**（标题 36px，小标题 34px，条目 30px）
7. 要领区用 **分段布局 + stretch** 填满竖向空间，避免下方大块空白

### 代码位置

- `skillTipsPoints()` / `skillTipsWarnings()` / `refreshTipsContent()`
- 常量：`kSkillDetailVideoW`、`kSkillDetailStartBtnW`、`kSkillDetailLeftColW`

详见 [`single-practice-ui.md`](single-practice-ui.md)。

---

## 5. 其他 UI（会话内已完成）

- **SinglePage / PracticePage**：去左侧栏，按钮放大
- **SinglePracticeSetupPage**：「下一步」始终显示，扫到设备即可点
- **TrainingPage 单人模式**：击球类型 `refreshFixedSkillTypeLabel()` 写死为当前技能

---

## 6. 部署与排错实录

| 现象 | 原因 | 处理 |
|------|------|------|
| `widget_panel` Exec format error | 0 字节二进制 | `ensure_panel_binary()`；等 make 完成再 run |
| `vb_set_conf failed` | MPP/VB 未释放 | reboot 或完整 kill 后仍失败则 reboot |
| `make` 被 kill | deploy 90s 超时 | 板端单独 make，block_until ≥180s |
| `main.moc` redefinition | `main.cpp` 末尾重复 `#include "main.moc"` | 只保留一处 |
| `vo_gfbg_init` Text file busy | 进程占用 | stop service / killall 再 scp |
| service inactive 但 run.sh exit 0 | vo_gfbg_init 失败仍退出 | 需 reboot 清 MPP；成功时 vo_gfbg_init 阻塞到 panel 结束 |
| 完整 deploy.sh 卡住 | 上传量大 + 板端编译慢 | 增量：`scp main.cpp` + 板端 `make` |

### 推荐增量部署（改 UI / main.cpp）

```bash
cd version3.0
sshpass -p ebaina scp -o StrictHostKeyChecking=no src/main.cpp root@192.168.1.168:/opt/widget_ui/main.cpp
sshpass -p ebaina ssh root@192.168.1.168 "cd /opt/widget_ui && rm -f main.o main.moc && make && systemctl restart widget_ui.service"
```

改 `sample_vio_ai.c` / `vo_gfbg_init.c` 需在 PC 交叉编译后 scp 到板端。

---

## 7. 待用户现场微调项

- 单人练习击球灵敏度：`WIDGET_PRACTICE_*`、`WIDGET_IMU_*`（见 run.sh）
- 动作要领文案：改 `skillTipsPoints/Warnings()` in `main.cpp`
- 教学视频：(`/opt/widget_ui/tutorials/<技能名>.mp4`，H.264 无音轨)

---

## 8. 相关 Git 状态说明

会话中修改集中在 `version3.0/`，未要求 commit 时不要主动提交。  
对象文件、板端编译产物勿入库。

---

## 9. 骨骼 Pose RGN（2026-07）

### 完成内容

- 集成 `best_pose_aipp.om`（640×640 → 17 关键点 + skeleton）
- RGN LINE 叠加到 VPSS ch0 预览；参考 YOLOs-CPP 自适应线宽（cap 8）
- 稳定化：`pose_apply_stabilized`（snap / 滞后可见性 / bbox 大跳才 hard_reset）
- 环境变量：`WIDGET_POSE_*`（见 `run.sh`）

### 关键文件

- `ai/sample_vio_ai.c`：`ai_pose_infer`、`pose_rgn_*`、`pose_postprocess_and_draw`

详见 [`hit-replay-and-pose-rgn.md`](hit-replay-and-pose-rgn.md)。

---

## 10. 单人击球回放（2026-07）

### 完成内容

- 击球触发 → 环形缓冲 pre 2s + post 2s → 导出 PPM + `meta.txt` + `done.flag`
- Qt `ActionDetailPage` / `FrameReplayWidget` 播放（板端无 ffmpeg 时走 PPM 目录）
- 回放帧软件叠加骨骼：`pose_stamp_on_replay_nv12()`
- 修复：离开训练页勿 `clearReplaySession`；`hit_replay_needs_frames()` 保证 post 采集完成
- 输出 **960×540**，640 源 letterbox；**禁止从 ch0 抢帧**（曾导致骨骼消失、进程崩溃）

### 关键文件

- `ai/sample_vio_ai.c`：`hit_replay_*`
- `src/pages_training.cpp`、`src/main_window.cpp`、`src/ui_common.cpp`
- `scripts/deploy_bin.sh`、`scripts/deploy_panel.sh`

### 部署

```bash
bash scripts/build_vio_ai.sh && bash scripts/deploy_bin.sh
bash scripts/deploy_panel.sh main_window.cpp pages_training.cpp ui_pages.h
```

改 AI 后若 MPP 异常需 **reboot** 再 `bash run.sh`。

---

## 11. 对打模式多拍柄（2026-07）

- `MatchSetupPage`：扫描绑定最多 4 台九轴
- `MatchRunningPage`：多设备布局、IMU 触发、视觉延迟更新、评分建议
- 页面路由 page 16、`sle_imu` 流与 `WidgetYoloAction` swing 时间戳缓冲

代码：`pages_match.cpp`、`main_window.cpp`、`widget_yolo_action.cpp` 等。

---

## 12. 源码模块化（pages 拆分）

`main.cpp` 已拆为：

- `pages_home.cpp`、`pages_practice.cpp`、`pages_training.cpp`
- `pages_match.cpp`、`pages_class.cpp`
- `main_window.cpp`、`ui_common.cpp`、`ui_pages.h`

增量部署优先 `deploy_panel.sh`（md5 对比 + 只重编改动 .o）。
