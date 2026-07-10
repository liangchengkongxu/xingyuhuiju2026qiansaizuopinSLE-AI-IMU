#!/bin/sh
# 星闪广播扫描桥：输出 SLE_DEVICE 行到 /tmp/sle_seek_lines（供 Qt 扫描页读取）
WS73_ROOT="${WS73_ROOT:-/opt/widget_ui/ws73}"
. "$WS73_ROOT/ws73_common.sh"

LOG="/tmp/sle_seek_lines"
PIDF="/tmp/sle_seek_bridge.pid"
BIN="$WS73_ROOT/sle_seek_print_all"
SCAN_SEC="${SLE_SEEK_SCAN_SEC:-6}"
PREP_STAMP="/tmp/ws73_seek_prep.stamp"
PREP_PIDF="/tmp/ws73_seek_prep.pid"
PREP_MAX_AGE="${SLE_SEEK_PREP_MAX_AGE:-120}"

stop_seek() {
    killall sle_seek_print_all 2>/dev/null
    if [ -f "$PIDF" ]; then
        kill "$(cat "$PIDF")" 2>/dev/null
        rm -f "$PIDF"
    fi
}

mark_prep_ready() {
    date +%s > "$PREP_STAMP"
}

# 扫描前必须完整复位 WS73（mcu nl + rmmod/insmod），否则易出现 sle adapter init open fail
sle_seek_radio_ready() {
    "$WS73_ROOT/sle_imu_bridge.sh" stop 2>/dev/null || true
    killall sle_connect_imu sle_announce_cmd sle_client_sample 2>/dev/null

    if [ -f "$PREP_STAMP" ]; then
        now=$(date +%s)
        age=$((now - $(cat "$PREP_STAMP")))
        if [ "$age" -lt "$PREP_MAX_AGE" ] && ws73_sle_loaded; then
            ws73_prep_radio_light
            ws73_insmod_sle
            return 0
        fi
    fi

    ws73_prep_radio
    ws73_insmod_sle
    mark_prep_ready
}

# 进入扫描页时预热射频（不启动扫描）
prep_seek() {
    if [ -f "$PREP_PIDF" ] && kill -0 "$(cat "$PREP_PIDF")" 2>/dev/null; then
        exit 0
    fi
    (
        sle_seek_radio_ready
        mark_prep_ready
    ) &
    echo $! > "$PREP_PIDF"
    wait "$(cat "$PREP_PIDF")" 2>/dev/null
    rm -f "$PREP_PIDF"
}

start_seek() {
    if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then
        exit 0
    fi
    stop_seek
    : > "$LOG"

    (
        if ! sle_seek_radio_ready; then
            echo "[bridge] radio prep failed (check $WS73_KO/plat_soc.ko sle_soc.ko)" >> "$LOG"
            exit 1
        fi
        if [ ! -x "$BIN" ]; then
            echo "[bridge] missing $BIN — PC: bash version3.0/scripts/deploy_ws73.sh" >> "$LOG"
            exit 1
        fi
        # 勿继承 run.sh 的 SLE_SEEK_MAX_RX=1（interval/window=8 会导致 0 设备）
        unset SLE_SEEK_MAX_RX SLE_SEEK_INTERVAL SLE_SEEK_WINDOW
        export SLE_SEEK_QUIET=1
        timeout "$SCAN_SEC" stdbuf -oL -eL "$BIN" 2>>"$LOG" | while IFS= read -r line; do
            case "$line" in
            SLE_DEVICE\|*)
                echo "$line" >> "$LOG"
                ;;
            "[SLE_SEEK]"*"扫描已开启"|"[SLE_SEEK]"*"星闪已开启")
                echo "$line" >> "$LOG"
                ;;
            "[SLE_SEEK]"*"init open fail"|"[SLE_SEEK]"*"gle init failed"|"[Error]"*)
                echo "$line" >> "$LOG"
                ;;
            esac
        done
        echo "[bridge] scan done" >> "$LOG"
    ) &

    echo $! > "$PIDF"
}

case "$1" in
start) start_seek ;;
stop) stop_seek ;;
prep) prep_seek ;;
*) echo "usage: $0 {start|stop|prep}"; exit 1 ;;
esac
