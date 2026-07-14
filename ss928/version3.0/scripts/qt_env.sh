#!/bin/bash
# Qt 5 运行时环境变量 — SS928 linuxfb + 触摸

# 显示：linuxfb 直接渲染到 GFBG 管理的帧缓冲（与系统 Qt5.15 + Multimedia 插件一致）
export QT_QPA_PLATFORM=linuxfb:fb=/dev/fb0:size=1920x1080
export QT_QPA_PLATFORM_PLUGIN_PATH=/usr/lib/aarch64-linux-gnu/qt5/plugins/platforms
export QT_PLUGIN_PATH=/usr/lib/aarch64-linux-gnu/qt5/plugins
export QT_QPA_FB_HIDECURSOR=1

# 字体
export QT_QPA_FONTDIR=/opt/fonts

# 库路径：系统 Qt 必须优先于 /opt/lib 自带 Qt，避免 5.15.3/5.15.16 混用崩溃
export LD_LIBRARY_PATH=/opt/lib/npu:/usr/lib/aarch64-linux-gnu:/opt/lib:${LD_LIBRARY_PATH}
export ASCEND_AICPU_KERNEL_PATH="${ASCEND_AICPU_KERNEL_PATH:-/opt/lib/npu}"

# 触摸（由 run.sh + detect_touch.sh 设置 TOUCH_DEVICE 与 evdev 参数）
# 可选微调（不改 UI 旋转，仅校准触摸）：
#   WIDGET_TOUCH_ROTATE=0|90|180|270
#   WIDGET_TOUCH_INVERT_X=1  WIDGET_TOUCH_INVERT_Y=1  WIDGET_TOUCH_SWAP_XY=1
if [ -n "$TOUCH_DEVICE" ] && [ -f "$(dirname "${BASH_SOURCE[0]}")/detect_touch.sh" ]; then
    # shellcheck source=/dev/null
    source "$(dirname "${BASH_SOURCE[0]}")/detect_touch.sh"
fi
