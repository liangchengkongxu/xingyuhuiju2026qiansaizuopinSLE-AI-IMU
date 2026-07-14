#!/bin/bash
# 自动探测触摸屏设备并导出 TOUCH_DEVICE / QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS
# 用法: source detect_touch.sh

TOUCH_DEVICE="${TOUCH_DEVICE:-}"

touch_device_is_touchscreen() {
    local dev="$1"
    [ -n "$dev" ] && [ -c "$dev" ] || return 1
    if udevadm info -q property -n "$dev" 2>/dev/null | grep -qi 'ID_INPUT_TOUCHSCREEN=1'; then
        return 0
    fi
    return 1
}

build_widget_touch_evdev_params() {
    if [ -z "$TOUCH_DEVICE" ]; then
        unset QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS
        return 1
    fi
    local p="$TOUCH_DEVICE"
    p="${p}:rotate=${WIDGET_TOUCH_ROTATE:-0}"
    [ "${WIDGET_TOUCH_INVERT_X:-0}" = "1" ] && p="${p}:invertx"
    [ "${WIDGET_TOUCH_INVERT_Y:-0}" = "1" ] && p="${p}:inverty"
    [ "${WIDGET_TOUCH_SWAP_XY:-0}" = "1" ] && p="${p}:swapxy"
    export QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS="$p"
    return 0
}

if [ -n "$TOUCH_DEVICE" ] && touch_device_is_touchscreen "$TOUCH_DEVICE"; then
    export TOUCH_DEVICE
    build_widget_touch_evdev_params
    echo "detect_touch: 使用已有触摸设备 $TOUCH_DEVICE"
    echo "detect_touch: QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=$QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS"
    return 0 2>/dev/null || true
fi

TOUCH_DEVICE=""

# 方法1: udev 标记的触摸屏（最可靠）
for dev in /dev/input/event*; do
    [ -c "$dev" ] || continue
    if touch_device_is_touchscreen "$dev"; then
        TOUCH_DEVICE="$dev"
        break
    fi
done

# 方法2: /proc/bus/input/devices — 名称含 touch/tp/capacitive 且 Handlers 含 eventN
if [ -z "$TOUCH_DEVICE" ] && [ -f /proc/bus/input/devices ]; then
    while IFS= read -r dev; do
        [ -n "$dev" ] && [ -c "$dev" ] || continue
        TOUCH_DEVICE="$dev"
        break
    done < <(awk '
        /^N: Name="/ {
            name = $0
            sub(/^N: Name="/, "", name)
            sub(/"$/, "", name)
        }
        /^H: Handlers=/ {
            if (name ~ /[Tt]ouch|[Tt][Pp]|Capacitive|capacitive|GOODIX|Goodix|GT[0-9]/) {
                if (match($0, /event[0-9]+/)) {
                    print "/dev/input/" substr($0, RSTART, RLENGTH)
                }
            }
        }
    ' /proc/bus/input/devices)
fi

# 方法3: evtest 查询 ABS（板端有 evtest 时）
if [ -z "$TOUCH_DEVICE" ] && command -v evtest >/dev/null 2>&1; then
    for dev in /dev/input/event*; do
        [ -c "$dev" ] || continue
        if timeout 0.4 evtest --query "$dev" EV_ABS ABS_X 2>/dev/null | grep -q 'Value'; then
            TOUCH_DEVICE="$dev"
            break
        fi
    done
fi

if [ -n "$TOUCH_DEVICE" ]; then
    export TOUCH_DEVICE
    build_widget_touch_evdev_params
    echo "detect_touch: 找到触摸设备 $TOUCH_DEVICE"
    echo "detect_touch: QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS=$QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS"
else
    echo "detect_touch: 未找到触摸设备"
fi
