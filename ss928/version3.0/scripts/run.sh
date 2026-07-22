#!/bin/bash
# SS928 Widget 面板启动脚本
set -e

APP_DIR="/opt/widget_ui"
APP_NAME="widget_panel"
BOOT_START=$(date +%s)

cd "$APP_DIR"

# 击球回放仅本机运行期有效：每次启动清掉上次残留（关机/重启后不保留，避免占满磁盘）
REPLAY_DIR="${WIDGET_REPLAY_DIR:-$APP_DIR/replays}"
if [ "${WIDGET_REPLAY_KEEP:-0}" != "1" ]; then
    rm -rf "${REPLAY_DIR}" 2>/dev/null || true
fi
mkdir -p "${REPLAY_DIR}"
export WIDGET_REPLAY_DIR="${REPLAY_DIR}"

boot_log() {
    echo "[boot +$(( $(date +%s) - BOOT_START ))s] $*" >> /tmp/widget_ui_boot.log 2>/dev/null || true
}

if [ "${WIDGET_BOOT_FAST:-1}" = "1" ]; then
    boot_log "fast mode start $(date '+%H:%M:%S')"
fi

# ─── 确保 panel 可执行（勿依赖 file 命令，板端常无此工具导致每次开机全量 make）───
panel_binary_ok() {
    local bin="$1"
    local sz
    [ -f "$bin" ] && [ -x "$bin" ] || return 1
    sz=$(stat -c%s "$bin" 2>/dev/null || wc -c < "$bin" 2>/dev/null || echo 0)
    [ "${sz:-0}" -gt 50000 ] || return 1
    # ELF magic
    [ "$(head -c 4 "$bin" 2>/dev/null | od -An -tx1 | tr -d ' \n')" = "7f454c46" ] && return 0
    # 可选：有 file 时再校验架构
    if command -v file >/dev/null 2>&1; then
        file "$bin" 2>/dev/null | grep -qE 'ARM aarch64|aarch64|ELF.*64-bit' && return 0
    fi
    # 大体积可执行 ELF 头偶尔读不到时仍放行，避免反复编译
    [ "${sz:-0}" -gt 200000 ]
}

build_lock() {
    # 防止 systemd Restart 与手动 run.sh 并发编译，导致 0 字节 widget_panel
    exec 9>"$APP_DIR/.widget_panel_build.lock"
    if ! flock -n 9; then
        echo "  等待其他编译任务结束..."
        flock 9
    fi
}

fix_make_clock_skew() {
    if make -C "$APP_DIR" -n widget_panel 2>&1 | grep -qi 'clock skew\|in the future'; then
        echo "  检测到时钟偏差，修正源文件时间戳..."
        find "$APP_DIR" -maxdepth 1 \( -name '*.cpp' -o -name '*.h' -o -name 'Makefile' -o -name '*.moc' \) \
            -exec touch -d "@$(date +%s)" {} + 2>/dev/null || true
    fi
}

ensure_panel_binary() {
    if [ "${WIDGET_PANEL_SKIP_BUILD:-0}" = "1" ] && [ -f "$APP_DIR/$APP_NAME" ] && [ -s "$APP_DIR/$APP_NAME" ]; then
        return 0
    fi
    if panel_binary_ok "$APP_DIR/$APP_NAME"; then
        echo "  使用已有 $APP_NAME ($(stat -c%s "$APP_DIR/$APP_NAME" 2>/dev/null || wc -c < "$APP_DIR/$APP_NAME") bytes)"
        return 0
    fi
    build_lock
    echo "  $APP_NAME 缺失或无效，正在板端编译（约 1–2 分钟）..."
    rm -f "$APP_DIR/$APP_NAME.tmp"
    # 必须删除无效/空文件，否则 make 认为 target 已是最新而跳过链接
    rm -f "$APP_DIR/$APP_NAME"
    fix_make_clock_skew
    if ! make -C "$APP_DIR" widget_panel; then
        echo "错误: make 失败，请检查上方编译输出"
        rm -f "$APP_DIR/$APP_NAME" "$APP_DIR/$APP_NAME.tmp"
        return 1
    fi
    if [ ! -s "$APP_DIR/$APP_NAME" ]; then
        echo "错误: 编译后 $APP_NAME 仍为空，请勿在 make 完成前再次运行 run.sh"
        return 1
    fi
    if ! panel_binary_ok "$APP_DIR/$APP_NAME"; then
        echo "错误: $APP_NAME 校验失败（非有效 ELF 可执行文件）"
        ls -l "$APP_DIR/$APP_NAME" 2>/dev/null || true
        return 1
    fi
    echo "  编译完成: $(ls -lh "$APP_DIR/$APP_NAME" | awk '{print $5, $9}')"
}
ensure_panel_binary || exit 1
boot_log "panel binary ready"

# ─── 自动探测触摸设备 ───
TOUCH_DEVICE=""
touch_cache_valid() {
    local dev="$1"
    [ -n "$dev" ] && [ -c "$dev" ] || return 1
    udevadm info -q property -n "$dev" 2>/dev/null | grep -qi 'ID_INPUT_TOUCHSCREEN=1'
}

if [ "${WIDGET_TOUCH_RESCAN:-0}" != "1" ] && [ "${WIDGET_BOOT_FAST:-1}" = "1" ] && [ -f /tmp/.widget_touch_device ]; then
    _cached_touch=$(cat /tmp/.widget_touch_device 2>/dev/null)
    if touch_cache_valid "$_cached_touch"; then
        TOUCH_DEVICE="$_cached_touch"
    else
        rm -f /tmp/.widget_touch_device 2>/dev/null || true
    fi
fi
if [ -z "$TOUCH_DEVICE" ] && [ -f "$APP_DIR/detect_touch.sh" ]; then
    # shellcheck source=/dev/null
    source "$APP_DIR/detect_touch.sh"
    if [ -n "$TOUCH_DEVICE" ]; then
        echo "$TOUCH_DEVICE" > /tmp/.widget_touch_device 2>/dev/null || true
    fi
elif [ -n "$TOUCH_DEVICE" ] && [ -f "$APP_DIR/detect_touch.sh" ]; then
    # shellcheck source=/dev/null
    source "$APP_DIR/detect_touch.sh"
fi

# 如果探测到触摸设备，设置 evdev 触摸参数
if [ -n "$TOUCH_DEVICE" ]; then
    echo "  触摸设备: $TOUCH_DEVICE"
    echo "  触摸参数: ${QT_QPA_EVDEV_TOUCHSCREEN_PARAMETERS:-未设置}"
else
    echo "  未探测到触摸设备，将以纯显示模式运行"
fi

# ─── 停止旧进程 ───
if [ "${WIDGET_BOOT_FAST:-1}" = "1" ]; then
    boot_log "kill stale widget/camera"
    killall vo_gfbg_init "$APP_NAME" sample_vio_ai sample_yolov8_os08a20 2>/dev/null || true
    sleep 0.2
else
    echo "[1] Stopping old processes..."
    killall vo_gfbg_init "$APP_NAME" sample_vio_ai sample_yolov8_os08a20 sle_seek_print_all sle_connect_imu sle_announce_cmd 2>/dev/null || true
    if [ -x "$APP_DIR/ws73/sle_imu_bridge.sh" ]; then
        "$APP_DIR/ws73/sle_imu_bridge.sh" stop 2>/dev/null || true
    fi
    if [ -f "$APP_DIR/ws73/ws73_common.sh" ]; then
        # shellcheck source=/dev/null
        . "$APP_DIR/ws73/ws73_common.sh"
        if [ "${WIDGET_WS73_FORCE_RMMOD:-0}" = "1" ]; then
            ws73_rmmod_sle 2>/dev/null || true
        fi
    fi
    sleep 1
fi

# 挥拍来源需在 boot prep 之前确定（默认 camera）
export WIDGET_WS73_KEEP_WIFI="${WIDGET_WS73_KEEP_WIFI:-1}"
export WIDGET_WIFI_ENABLE="${WIDGET_WIFI_ENABLE:-1}"
export WIDGET_WIFI_BOOT_FAST="${WIDGET_WIFI_BOOT_FAST:-1}"
export WIDGET_WIFI_WATCH_SEC="${WIDGET_WIFI_WATCH_SEC:-30}"
export WIDGET_WS73_SKIP_WIFI="${WIDGET_WS73_SKIP_WIFI:-1}"
export WIDGET_WS73_SKIP_LEGACY_CLIENT="${WIDGET_WS73_SKIP_LEGACY_CLIENT:-1}"
export WIDGET_CLOUD_ENABLE="${WIDGET_CLOUD_ENABLE:-1}"
export WIDGET_HIT_SOURCE="${WIDGET_HIT_SOURCE:-both}"
# 班级模式：拍柄九轴 1D CNN 击球类型（badminton_npu.om）
export WIDGET_IMU_CNN_MODEL="${WIDGET_IMU_CNN_MODEL:-/opt/widget_ui/models/badminton_npu.om}"
# export WIDGET_IMU_CNN_DISABLE=1  # 设为 1 可关闭 CNN，仅统计挥拍
# 击球触发：rule=规则 FSM | cnn=仅 CNN 峰值 | both=CNN 优先 + 规则补漏
export WIDGET_IMU_HIT_MODE="${WIDGET_IMU_HIT_MODE:-both}"
export WIDGET_IMU_CNN_TRIGGER_CONF="${WIDGET_IMU_CNN_TRIGGER_CONF:-22}"
export WIDGET_IMU_CNN_COOLDOWN_MS="${WIDGET_IMU_CNN_COOLDOWN_MS:-500}"
export WIDGET_IMU_CNN_TRIGGER_MIN_M="${WIDGET_IMU_CNN_TRIGGER_MIN_M:-46}"
export WIDGET_IMU_CNN_TRIGGER_MIN_DYN="${WIDGET_IMU_CNN_TRIGGER_MIN_DYN:-6}"
export WIDGET_IMU_CNN_SOFT_MIN_M="${WIDGET_IMU_CNN_SOFT_MIN_M:-44}"
export WIDGET_IMU_CNN_SOFT_MIN_DYN="${WIDGET_IMU_CNN_SOFT_MIN_DYN:-4}"
export WIDGET_IMU_CNN_SOFT_MIN_GYRO="${WIDGET_IMU_CNN_SOFT_MIN_GYRO:-38}"
export WIDGET_IMU_CNN_SOFT_CONF="${WIDGET_IMU_CNN_SOFT_CONF:-18}"
export WIDGET_IMU_CNN_CLASS_CONF="${WIDGET_IMU_CNN_CLASS_CONF:-16}"
export WIDGET_IMU_RULE_SWING_ON_DYN="${WIDGET_IMU_RULE_SWING_ON_DYN:-6}"
export WIDGET_IMU_RULE_MIN_PEAK_DYN="${WIDGET_IMU_RULE_MIN_PEAK_DYN:-6}"
export WIDGET_IMU_RULE_GYRO_ON="${WIDGET_IMU_RULE_GYRO_ON:-35}"
export WIDGET_IMU_RULE_MIN_PEAK_GYRO="${WIDGET_IMU_RULE_MIN_PEAK_GYRO:-28}"
export WIDGET_IMU_RULE_COOLDOWN_MS="${WIDGET_IMU_RULE_COOLDOWN_MS:-300}"
export WIDGET_IMU_BURST_MIN_DYN="${WIDGET_IMU_BURST_MIN_DYN:-7}"
export WIDGET_IMU_BURST_MIN_GYRO="${WIDGET_IMU_BURST_MIN_GYRO:-45}"
# 星闪接收：提高 BSLE 相对 WiFi 共存优先级（改 ini，勿用 SLE_SEEK_MAX_RX=1 的 8/8 扫描参数，会扫不到设备）
export WIDGET_WS73_BSLE_MAX_COEX="${WIDGET_WS73_BSLE_MAX_COEX:-1}"
export WIDGET_IMU_RULE_CONFIRM="${WIDGET_IMU_RULE_CONFIRM:-1}"
# 单人练习 UI 多源去重 / 摄像头触发阈值（防抬手误触、一次挥拍计两次）
export WIDGET_PRACTICE_HIT_COOLDOWN_MS="${WIDGET_PRACTICE_HIT_COOLDOWN_MS:-1500}"
export WIDGET_PRACTICE_CAM_STABLE_PERCENT="${WIDGET_PRACTICE_CAM_STABLE_PERCENT:-40}"
export WIDGET_PRACTICE_CAM_SWING_PERCENT="${WIDGET_PRACTICE_CAM_SWING_PERCENT:-40}"
# export WIDGET_IMU_CNN_DEBUG=1  # 打开后在 /tmp/widget_imu.log 打印 6 类概率

# ─── WiFi：rc.local S92wifi 已 insmod，此处只等 wlan0 + 看门狗修 wpa ───
set +e
if [ "${WIDGET_WIFI_ENABLE}" = "1" ] && [ -x "$APP_DIR/ws73/wifi_sta_bridge.sh" ]; then
    echo "[1.1] WiFi: 等 S92wifi / widget_wifi（不重复 insmod）..."
    if ! pgrep -f "wifi_sta_bridge.sh watch" >/dev/null 2>&1; then
        nohup /bin/sh "$APP_DIR/ws73/wifi_sta_bridge.sh" watch >> /tmp/wifi_sta_bridge.log 2>&1 &
    fi
fi
set -e

# ─── 开机预热星闪（WiFi 就绪后再加载 sle，避免 wlan0 不出现）───
export WIDGET_WS73_BOOT_PREP="${WIDGET_WS73_BOOT_PREP:-1}"
if [ "$WIDGET_WS73_BOOT_PREP" = "1" ] && [ -x "$APP_DIR/ws73/ws73_boot_prep.sh" ]; then
    if [ "${WIDGET_HIT_SOURCE}" = "camera" ]; then
        echo "[1.2] WS73 boot prep (background, after WiFi)..."
        (
            i=0
            while [ "$i" -lt 30 ]; do
                ip link show wlan0 >/dev/null 2>&1 && break
                sleep 1
                i=$((i + 1))
            done
            /bin/sh "$APP_DIR/ws73/ws73_boot_prep.sh"
        ) >/dev/null 2>&1 &
    fi
fi

# ─── 摄像头预览 + AI attach（默认）；清理残留 ISP ───
export WIDGET_AI_BACKEND="${WIDGET_AI_BACKEND:-attach}"
export WIDGET_AI_MODE="${WIDGET_AI_MODE:-cls}"
export WIDGET_CAM_MIPI="${WIDGET_CAM_MIPI:-1}"
export WIDGET_CAM_ISP_CLEAN="${WIDGET_CAM_ISP_CLEAN:-1}"
export WIDGET_YOLO_SHOW_MAX="${WIDGET_YOLO_SHOW_MAX:-0}"
export WIDGET_YOLO_BOX_DRAW="${WIDGET_YOLO_BOX_DRAW:-0}"
export WIDGET_YOLO_DET_DISABLE="${WIDGET_YOLO_DET_DISABLE:-1}"
export WIDGET_YOLO_DISP_THREAD="${WIDGET_YOLO_DISP_THREAD:-0}"
export WIDGET_YOLO_TARGET="${WIDGET_YOLO_TARGET:-person}"
export WIDGET_YOLO_DET_MODEL="${WIDGET_YOLO_DET_MODEL:-/opt/yolov8n.om}"
export WIDGET_YOLO_DET_CHN="${WIDGET_YOLO_DET_CHN:-2}"
export WIDGET_YOLO_PERSON_CONF="${WIDGET_YOLO_PERSON_CONF:-0.25}"
export WIDGET_YOLO_PERSON_SMOOTH="${WIDGET_YOLO_PERSON_SMOOTH:-0.55}"
export WIDGET_YOLO_SMOOTH="${WIDGET_YOLO_SMOOTH:-0.26}"
export WIDGET_YOLO_SMOOTH_SIZE="${WIDGET_YOLO_SMOOTH_SIZE:-0.15}"
export WIDGET_YOLO_DISPLAY_SMOOTH="${WIDGET_YOLO_DISPLAY_SMOOTH:-0.30}"
export WIDGET_YOLO_DISP_FPS="${WIDGET_YOLO_DISP_FPS:-40}"
export WIDGET_YOLO_TRACK_IOU="${WIDGET_YOLO_TRACK_IOU:-0.06}"
export WIDGET_YOLO_HOLD="${WIDGET_YOLO_HOLD:-12}"
export WIDGET_YOLO_FRAME_MS="${WIDGET_YOLO_FRAME_MS:-33}"
export WIDGET_YOLO_DRAW_NV12="${WIDGET_YOLO_DRAW_NV12:-0}"
# action=YOLOv8 动作类最高分框（默认，适配 best_aipp_fix.om 六类）
export WIDGET_YOLO_TARGET="${WIDGET_YOLO_TARGET:-action}"
export WIDGET_YOLO_ACTION="${WIDGET_YOLO_ACTION:-1}"
# 动作/挥拍置信度阈值（%），低于此值不显示、不计挥拍
export WIDGET_YOLO_CONF_PERCENT="${WIDGET_YOLO_CONF_PERCENT:-34}"
export WIDGET_YOLO_SWING="${WIDGET_YOLO_SWING:-1}"
export WIDGET_YOLO_SWING_FIRE_PERCENT="${WIDGET_YOLO_SWING_FIRE_PERCENT:-40}"
export WIDGET_YOLO_SWING_VEL="${WIDGET_YOLO_SWING_VEL:-0.18}"
export WIDGET_YOLO_SWING_PEAK="${WIDGET_YOLO_SWING_PEAK:-0.28}"
export WIDGET_YOLO_SWING_COOLDOWN_MS="${WIDGET_YOLO_SWING_COOLDOWN_MS:-1500}"
export WIDGET_YOLO_SERVE_SCALE="${WIDGET_YOLO_SERVE_SCALE:-0.05}"
export WIDGET_YOLO_SERVE_WIN_RATIO="${WIDGET_YOLO_SERVE_WIN_RATIO:-3.5}"
# YOLOv8 Pose 人体骨骼（VPSS ch1 224 推理；实时 RGN 默认关，回放由软件 stamp）
export WIDGET_POSE_ENABLE="${WIDGET_POSE_ENABLE:-1}"
export WIDGET_POSE_RGN="${WIDGET_POSE_RGN:-0}"
export WIDGET_POSE_MODEL="${WIDGET_POSE_MODEL:-/opt/widget_ui/models/best_pose_aipp.om}"
export WIDGET_POSE_CHN="${WIDGET_POSE_CHN:-2}"
export WIDGET_POSE_CONF="${WIDGET_POSE_CONF:-0.10}"
export WIDGET_POSE_IOU="${WIDGET_POSE_IOU:-0.45}"
export WIDGET_POSE_KPT_VIS="${WIDGET_POSE_KPT_VIS:-0.25}"
export WIDGET_POSE_LINE_THICK="${WIDGET_POSE_LINE_THICK:-7}"
export WIDGET_POSE_LINE_AUTO="${WIDGET_POSE_LINE_AUTO:-1}"
export WIDGET_POSE_BOX_DRAW="${WIDGET_POSE_BOX_DRAW:-0}"
export WIDGET_POSE_CH1_ONLY="${WIDGET_POSE_CH1_ONLY:-1}"
export WIDGET_POSE_INTERVAL="${WIDGET_POSE_INTERVAL:-2}"
export WIDGET_POSE_HOLD_MS="${WIDGET_POSE_HOLD_MS:-120}"
export WIDGET_POSE_MOTION_PX="${WIDGET_POSE_MOTION_PX:-16}"
export WIDGET_POSE_STABLE_PX="${WIDGET_POSE_STABLE_PX:-12}"
export WIDGET_POSE_KPT_SNAP_PX="${WIDGET_POSE_KPT_SNAP_PX:-8}"
export WIDGET_POSE_MISS_MAX="${WIDGET_POSE_MISS_MAX:-3}"
export WIDGET_POSE_CLEAR_ON_ACTION="${WIDGET_POSE_CLEAR_ON_ACTION:-0}"
export WIDGET_POSE_CLEAR_ON_SWING="${WIDGET_POSE_CLEAR_ON_SWING:-1}"
export WIDGET_POSE_BBOX_JUMP_PX="${WIDGET_POSE_BBOX_JUMP_PX:-28}"
export WIDGET_POSE_SMOOTH_ALPHA="${WIDGET_POSE_SMOOTH_ALPHA:-0.35}"
export WIDGET_REPLAY_LIVE="${WIDGET_REPLAY_LIVE:-1}"
export WIDGET_REPLAY_FPS="${WIDGET_REPLAY_FPS:-25}"
export WIDGET_REPLAY_POSE_EAGER_MAX="${WIDGET_REPLAY_POSE_EAGER_MAX:-3}"
export WIDGET_REPLAY_VPSS_CHN="${WIDGET_REPLAY_VPSS_CHN:-3}"
export WIDGET_REPLAY_SRC_NV21="${WIDGET_REPLAY_SRC_NV21:-1}"
export WIDGET_AI_ATTACH_DELAY_SEC="${WIDGET_AI_ATTACH_DELAY_SEC:-2}"
# WIDGET_HIT_SOURCE 已在 boot prep 前 export
# ─── 星闪 IMU 广播桥（仅 imu/both 模式需要；失败不阻塞面板）───
set +e
if [ "${WIDGET_HIT_SOURCE}" != "camera" ] && [ -x "$APP_DIR/ws73/sle_imu_bridge.sh" ]; then
    echo "[1.5] Starting IMU broadcast bridge (WIDGET_HIT_SOURCE=$WIDGET_HIT_SOURCE)..."
    "$APP_DIR/ws73/sle_imu_bridge.sh" start || echo "[warn] sle_imu_bridge start failed" >> /tmp/widget_ui_boot.log 2>&1
fi
set -e

# ─── 初始化 VO/HDMI/GFBG ───
boot_log "vo_gfbg_init start (WIDGET_AI_BACKEND=${WIDGET_AI_BACKEND:-attach})"
# 系统 Qt 5.15.3 必须在 /opt/lib Qt 5.15.16 之前，否则 panel SIGABRT
export LD_LIBRARY_PATH="/opt/lib/npu:/usr/lib/aarch64-linux-gnu:/opt/lib:${LD_LIBRARY_PATH:-}"
# IMU 1D CNN / ACL 推理需加载 AICPU 算子（默认找 /usr/lib64/aicpu_kernels，板端在 /opt/lib/npu）
export ASCEND_AICPU_KERNEL_PATH="${ASCEND_AICPU_KERNEL_PATH:-/opt/lib/npu}"
if [ -f "$APP_DIR/vo_gfbg_init" ]; then
    LD_LIBRARY_PATH="$LD_LIBRARY_PATH" "$APP_DIR/vo_gfbg_init" 2>&1 || {
        echo "  警告: vo_gfbg_init 失败, 跳过硬件初始化"
    }
elif [ -f "$APP_DIR/gfbg_init.py" ]; then
    python3 "$APP_DIR/gfbg_init.py"
else
    echo "  警告: 未找到 vo_gfbg_init 或 gfbg_init.py, 跳过硬件初始化"
fi

# ─── Qt 环境变量 ───
source "$APP_DIR/qt_env.sh" 2>/dev/null || true

# panel 已由 vo_gfbg_init 内部 fork 启动并等待
boot_log "run.sh done (panel managed by vo_gfbg_init)"
