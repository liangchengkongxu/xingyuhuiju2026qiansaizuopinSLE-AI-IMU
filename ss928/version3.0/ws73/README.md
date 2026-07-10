# 星闪 WS73（归属 version2.0 面板工程）

板端安装目录：**`/opt/widget_ui/ws73/`**（与 Qt 面板 `/opt/widget_ui` 同目录，不再往 `/opt/sample/ws73` 写脚本）。

## 目录

| 路径 | 说明 |
|------|------|
| `ws73_common.sh` | 上电、加载 ko、与 WiFi 互斥处理 |
| `ko/` | `plat_soc.ko`、`sle_soc.ko`（首次从 `/opt/sample/ws73` 复制） |
| `sle_seek_print_all` | 扫描广播：输出 `SLE_DEVICE` 设备行、`[SLE_IMU] @...` IMU 行 |
| `sle_imu_bridge.sh` | 持续扫描，将 IMU 行写入 `/tmp/sle_imu_lines`（Qt 挥拍检测） |
| `sle_seek_bridge.sh` | 短时扫描（多人模式设备列表） |
| `sle_seek_print.sh` | 手工启停扫描 |
| `sle_connect_imu` | （已弃用）旧版 GATT 连接收 Notify，仅调试保留 |

## PC 部署

```bash
cd version2.0
bash scripts/deploy_ws73.sh
```

或与面板一起部署（`deploy.sh` 默认会调用上述脚本）：

```bash
bash scripts/deploy.sh
# 仅面板、不要星闪：WIDGET_DEPLOY_SKIP_WS73=1 bash scripts/deploy.sh
```

## 板端使用

```bash
# 广播扫描
/opt/widget_ui/ws73/sle_seek_print.sh 0
# Ctrl+C 或
/opt/widget_ui/ws73/sle_seek_print.sh 1

# IMU 广播桥（与 run.sh / 面板共用）
/opt/widget_ui/ws73/sle_imu_bridge.sh start
tail -f /tmp/sle_imu_lines

# 终端粗估球速（demo，从日志读 @ 行）
/opt/widget_ui/ws73/run_imu_speed_demo.sh
# 或仅测算法（无需硬件）
python3 /opt/widget_ui/ws73/imu_speed_demo.py --demo
```

## 源码说明

- `sle_seek_print_all/`：LTV 解码广播；星闪新固件 **22B 二进制 `EB 1A 02`**（ADV + Scan Response 双份）→ 内部转 `@` 行；蓝牙仍为 ASCII
- `sle_connect_imu/`：旧版连接路径（可选编译，默认不用）

工作区副本：`sle_broadcast_rx/ws73/` 为早期目录，**以本目录为准**。
