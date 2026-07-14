# 开机自启与快速启动

脚本：[`../scripts/widget_ui.service`](../scripts/widget_ui.service)、[`../scripts/install_autostart.sh`](../scripts/install_autostart.sh)、[`../scripts/run.sh`](../scripts/run.sh)。

---

## 服务名与冲突

- **启用**：`widget_ui.service`
- **禁用**：`kiosk.service`（Weston）、`ui-server.service`（8080 旧网页）

---

## 启动链优化

| 原状 | 现况 |
|------|------|
| `ExecStartPre=sleep 10` | 轮询 `/dev/fb0`，最多 20×1s |
| 等 multi-user + snap | `WantedBy=rc-local.service`，ko 加载后即起 |
| 冷启动 ISP 清理 | `WIDGET_CAM_ISP_CLEAN=0`（service 环境） |
| run.sh 重型 stop | `WIDGET_BOOT_FAST=1`：仅 kill camera/panel |

冷启动实测：约 **8s** 内见 widget（原 ~30s）。

---

## 安装自启

```bash
cd version3.0/scripts
# 必须 ssh 直写 unit，勿 scp unit 文件
bash install_autostart.sh

# 可选立即启动
INSTALL_AUTOSTART_START=1 bash install_autostart.sh
```

验证：

```bash
systemctl status widget_ui.service
systemd-analyze critical-chain widget_ui.service
tail -f /tmp/widget_ui_boot.log
```

---

## run.sh 与 systemd 行为

- `Type=simple`：`run.sh` 在 `vo_gfbg_init` 阻塞期间保持 active；若 vo_gfbg_init 立即失败退出，service 会变 inactive（需查 MPP）。
- 日志：`/tmp/widget_ui_boot.log`

---

## vo_gfbg_init 微优化

`vo_gfbg_init.c` 中 AI attach 前 sleep 缩短（相对原版）：

- 800ms → 350ms
- 300ms → 120ms

改后需 PC 编译 `build_vo_gfbg_init.sh` 并部署 `/opt/widget_ui/vo_gfbg_init`。

---

## 排错

| 现象 | 处理 |
|------|------|
| unit `bad-setting` | 检查 unit 是否 null 字节；重装 install_autostart |
| 仍见 Weston / 8080 页 | `systemctl disable kiosk ui-server` |
| 自启后无 panel | 查 boot.log、`vb_set_conf` → reboot |
