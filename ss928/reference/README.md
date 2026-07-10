# 参考代码说明

本目录为 **SS928 主控 IMU 链路** 的参考快照，与 2026-07 **远距离二进制**协议对齐。

## 包含文件

| 文件 | 说明 |
|------|------|
| `ws73/sle_imu_bridge.sh` | 后台扫描，写入 `/tmp/sle_imu_lines` |
| `ws73/sle_seek_print_all/sle_seek_print_client.c` | 扫描解析 + **EB 1A 优先** + 去重 |
| `ws73/sle_seek_print_all/sle_imu_adv.h` | 协议头定义 |

## 不包含

- `sle_seek_print_main.c`、`Makefile` 等编译脚手架（见完整工程 `version3.0/ws73/`）
- Qt 面板 `widget_panel` 源码（见 `ss928/docs/关键源码索引.md`）

## 同步

参考代码从量产分支 `version3.0` 摘录；若协议变更，请同步更新本目录并 bump `bs20/docs/固件变更记录.md`。
