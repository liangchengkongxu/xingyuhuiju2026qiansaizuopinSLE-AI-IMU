# SS928 Widget v3.0 — Agent 参考技能包

本目录是 **Cursor Agent 自用参考**，汇总 version3.0 相关文档与联调操作记录。  
面向板端 **`192.168.1.168`**、`/opt/widget_ui/`、Qt 面板 + camera_pipe + sample_vio_ai attach + 星闪 IMU。

> 仓库内**对外主文档**仍是 [`../README.md`](../README.md)；本目录侧重「Agent 二次开发 / 排错 / 会话延续」。

---

## 阅读顺序（新会话建议）

1. [`operations-log.md`](operations-log.md) — 近期实际改了什么、怎么部署、踩过哪些坑  
2. [`board-ops-cheatsheet.md`](board-ops-cheatsheet.md) — 编译、部署、验证命令  
3. [`hit-replay-and-pose-rgn.md`](hit-replay-and-pose-rgn.md) — 骨骼与回放（易踩坑）  
4. 按任务选读下方专题

---

## 专题文档

| 文件 | 内容 |
|------|------|
| [`architecture-and-deploy.md`](architecture-and-deploy.md) | attach 架构、目录、PC→板端部署流程 |
| [`imu-cnn-and-hit-detection.md`](imu-cnn-and-hit-detection.md) | 九轴 1D CNN、规则 FSM、单人练习三路 OR 触发 |
| [`boot-autostart-and-fast-start.md`](boot-autostart-and-fast-start.md) | systemd 自启、开机 ~8s 优化 |
| [`single-practice-ui.md`](single-practice-ui.md) | 单人练习页面 UI、动作要领、击球逻辑 |
| [`hit-replay-and-pose-rgn.md`](hit-replay-and-pose-rgn.md) | **骨骼 RGN + 单人击球回放**（采集、部署、踩坑） |
| [`sources-index.md`](sources-index.md) | 仓库内各 README 索引（勿重复维护） |

---

## 关键路径速查

| 路径 | 用途 |
|------|------|
| `version3.0/src/main.cpp` | Qt 全部页面、TrainingPage / SkillDetailPage |
| `version3.0/scripts/run.sh` | 板端启动与环境变量默认值 |
| `version3.0/scripts/widget_ui.service` | 开机自启 unit |
| `version3.0/scripts/install_autostart.sh` | 安装自启（**unit 必须 ssh 直写，勿 scp**） |
| `version3.0/ai/sample_vio_ai.c` | 摄像头 AI attach、YOLO 挥拍 |
| `version3.0/src/imu_*.cpp` | IMU 规则 + CNN |
| `/opt/widget_ui/` | 板端运行目录 |
| `/tmp/widget_ui_boot.log` | 自启 / run.sh 日志 |
| `/tmp/widget_imu.log` | IMU 击球 `hit(cnn)` / `hit(rule)` |

---

## Agent 约束（务必遵守）

1. **部署 panel**：优先 `scp main.cpp` + 板端 `make`（约 2 分钟），避免完整 `deploy.sh` 超时；`make` 超时建议 ≥180s。  
2. **勿并行** `make` 与 `run.sh`，否则 `widget_panel` 可能 0 字节。  
3. **MPP 占用**：`vb_set_conf failed` → `killall` 不够时需 **reboot**。  
4. **systemd unit**：用 `install_autostart.sh`（ssh  heredoc），scp 曾出现全 null 字节。  
5. **改 run.sh 环境变量** 后需 `systemctl restart widget_ui.service` 或 reboot。  
6. **单人练习击球类型**在 TrainingPage 固定为当前动作，不跟 CNN/YOLO 分类走。  
7. **回放勿从 VPSS ch0 取帧**（`WIDGET_REPLAY_VPSS_CHN` 保持 `-1`），否则骨骼 RGN 与 VO 预览会被饿死。

---

## 与 Cursor Skill 的关系

本目录位于工程内 `version3.0/skill/`，便于与代码同仓版本管理。  
若需在 Cursor 全局复用，可将本目录 symlink 或复制要点到 `~/.cursor/skills/` 下的自定义 skill。
