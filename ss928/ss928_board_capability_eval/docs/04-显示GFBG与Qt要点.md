# 04 — 显示：GFBG + Qt linuxfb 要点

SS928 上常见做法：

- **视频**：MPP VO 硬件输出
- **UI**：GFBG（`/dev/fb0`）+ Qt `linuxfb` 插件叠层

## 易踩坑：BUF_DOUBLE 导致 Qt 秒退

现象：Qt 窗口闪一下后进程退出（status=1）。

原因摘要：

1. GFBG 设为双缓冲时 `yres_virtual = 2 * yres`
2. Qt 重新 open fb 后尝试 `FBIOPUT_VSCREENINFO` 改回单缓冲尺寸
3. 驱动拒绝 → linuxfb 初始化失败 → Qt exit

## 推荐配置

| 项 | 推荐 |
|----|------|
| buf 模式 | `OT_FB_LAYER_BUF_NONE`（直通） |
| yres_virtual | 等于 yres（如 1080） |
| xres_virtual | 等于 xres（如 1920） |
| 父进程 fb fd | 初始化后尽量不要关，避免子进程二次 PUT |
| 环境变量 | `QT_QPA_PLATFORM=linuxfb`；`QT_QPA_FB_HIDECURSOR=1` |

## 测评怎么做

1. `ls -l /dev/fb0`
2. 读 `virtual_size` / 色深
3. 跑一个最小 Qt Widgets（全屏色块）长期不退出
4. 再测「VO 视频 + GFBG UI」同屏（需 MPP sample / 自研管道）

更细的 ioctl 顺序可参考完整 SDK 中 `sample/gfbg` 与厂商 GFBG 文档。
