# 板端操作速查

默认：`ssh root@192.168.1.168`，密码 `ebaina`。

---

## 日常

```bash
# 状态
systemctl status widget_ui.service
ps | grep -E 'widget_panel|vo_gfbg_init|sample_vio_ai'

# 手动启动（调试）
cd /opt/widget_ui && bash run.sh

# 重启服务
systemctl restart widget_ui.service

# 清 MPP 顽固占用
sync && reboot
```

---

## 日志

```bash
tail -f /tmp/widget_ui_boot.log
tail -f /tmp/sample_vio_ai.log | grep -E 'cls:|person_draw|yolo cam-swing|hit_replay|pose rgn|infer fps'
tail -f /tmp/widget_imu.log | grep hit
cat /tmp/.widget_yolo_action
tail -f /tmp/widget_class_train.log   # 班级
```

---

## PC → 板端增量部署

### 仅 Qt UI（main.cpp）

```bash
cd version3.0
sshpass -p ebaina scp -o StrictHostKeyChecking=no \
  src/main.cpp root@192.168.1.168:/opt/widget_ui/main.cpp
sshpass -p ebaina ssh root@192.168.1.168 \
  "cd /opt/widget_ui && rm -f main.o main.moc && make && ls -lh widget_panel"
sshpass -p ebaina ssh root@192.168.1.168 \
  "systemctl restart widget_ui.service"
```

> 板端 `make` 约 1–2 分钟，Agent 命令 timeout 建议 ≥180s。

### run.sh / systemd

```bash
scp version3.0/scripts/run.sh root@192.168.1.168:/opt/widget_ui/
cd version3.0/scripts && bash install_autostart.sh
```

### sample_vio_ai

```bash
bash version3.0/scripts/build_vio_ai.sh
bash version3.0/scripts/deploy_bin.sh    # 推荐：vo_gfbg_init + sample_vio_ai + run.sh
# 或手动 scp bin/sample_vio_ai → /opt/widget_ui/bin/
# 改 AI 后若 vb_set_conf failed → reboot 再 run.sh
```

### widget_panel（增量）

```bash
bash version3.0/scripts/deploy_panel.sh                    # 自动 md5 对比
bash version3.0/scripts/deploy_panel.sh pages_training.cpp # 指定文件
```

### vo_gfbg_init

```bash
bash version3.0/scripts/build_vo_gfbg_init.sh
# 先 stop/kill 再 scp（否则 Text file busy）
scp version3.0/bin/vo_gfbg_init root@192.168.1.168:/opt/widget_ui/
```

---

## 常见错误

| 错误 | 处理 |
|------|------|
| `Exec format error` | `file widget_panel`；空文件则 `make` |
| `vb_set_conf failed` | reboot |
| `Text file busy` vo_gfbg_init | killall 后 scp |
| service inactive | 看 boot.log；MPP 或 vo_gfbg 失败 |
| duplicate main.moc | main.cpp 末尾只保留一个 `#include "main.moc"` |
| deploy.sh 超时 | 改用增量 scp + 板端 make |

---

## 环境变量临时覆盖（板端）

```bash
export WIDGET_IMU_CNN_DEBUG=1
export WIDGET_PRACTICE_CAM_STABLE_PERCENT=58
cd /opt/widget_ui && bash run.sh
```

持久改默认值：编辑 `/opt/widget_ui/run.sh` 或 `widget_ui.service` 的 `Environment=`。
