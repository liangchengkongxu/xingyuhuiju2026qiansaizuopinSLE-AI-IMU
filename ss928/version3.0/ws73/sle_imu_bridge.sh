#!/bin/sh
# 星闪 IMU 广播接收桥：持续扫描拍柄广播，将 @ 行 IMU 写入 /tmp/sle_imu_lines
WS73_ROOT="${WS73_ROOT:-/opt/widget_ui/ws73}"
. "$WS73_ROOT/ws73_common.sh"

LOG="/tmp/sle_imu_lines"
PIDF="/tmp/sle_imu_bridge.pid"
WIFI_PIDF="/tmp/sle_imu_wifi_watch.pid"
BIN="$WS73_ROOT/sle_seek_print_all"

stop_bridge() {
    killall sle_seek_print_all 2>/dev/null
    if [ -f "$PIDF" ]; then
        kill "$(cat "$PIDF")" 2>/dev/null
        rm -f "$PIDF"
    fi
    if [ -f "$WIFI_PIDF" ]; then
        kill "$(cat "$WIFI_PIDF")" 2>/dev/null
        rm -f "$WIFI_PIDF"
    fi
}

start_bridge() {
    if [ -f "$PIDF" ] && kill -0 "$(cat "$PIDF")" 2>/dev/null; then
        if pgrep -x sle_seek_print_all >/dev/null 2>&1; then
            exit 0
        fi
    fi
    stop_bridge
    : > "$LOG"

    export WIDGET_WS73_BSLE_MAX_COEX="${WIDGET_WS73_BSLE_MAX_COEX:-1}"
    unset SLE_SEEK_MAX_RX SLE_SEEK_INTERVAL SLE_SEEK_WINDOW

    (
        while true; do
            sleep "${WIDGET_WIFI_WATCH_SEC:-60}"
            if ip link show wlan0 >/dev/null 2>&1; then
                ws73_wifi_userspace_fix >> "$LOG" 2>&1 || true
            fi
        done
    ) &
    echo $! > "$WIFI_PIDF"

    (
        radio_ok=0
        while true; do
            if [ "$radio_ok" != 1 ]; then
                if [ "${WIDGET_WS73_KEEP_WIFI:-1}" = "1" ]; then
                    if ! ip link show wlan0 >/dev/null 2>&1; then
                        echo "[bridge] 等待 wlan0（WiFi 先就绪再加载 sle）" >> "$LOG"
                        sleep 10
                        continue
                    fi
                    if ! ws73_insmod_sle; then
                        echo "[bridge] sle insmod failed, retry 10s" >> "$LOG"
                        sleep 10
                        continue
                    fi
                else
                    ws73_prep_radio_imu
                    if ! ws73_insmod_sle; then
                        echo "[bridge] insmod failed, retry 10s" >> "$LOG"
                        sleep 10
                        continue
                    fi
                fi
                radio_ok=1
            fi
            if [ ! -x "$BIN" ]; then
                echo "[bridge] missing $BIN" >> "$LOG"
                sleep 10
                continue
            fi
            echo "[bridge] 扫描拍柄 IMU 广播（无需连接/发令）" >> "$LOG"
            killall sle_seek_print_all sle_connect_imu sle_announce_cmd 2>/dev/null
            unset SLE_SEEK_MAX_RX SLE_SEEK_INTERVAL SLE_SEEK_WINDOW
            export SLE_SEEK_QUIET=1
            imu_cnt=0
            stdbuf -oL -eL "$BIN" 2>&1 | while IFS= read -r line; do
                case "$line" in
                *"[SLE_IMU]"*@*|*"[SLE_IMU_RAW]"*)
                    echo "$line" >> "$LOG"
                    imu_cnt=$((imu_cnt + 1))
                    if [ "$imu_cnt" -eq 1 ] || [ $((imu_cnt % 200)) -eq 0 ]; then
                        echo "[bridge] 已收到 IMU 广播包 #$imu_cnt" >> "$LOG"
                    fi
                    ;;
                esac
            done
            echo "[bridge] sle_seek_print_all exited, retry 2s" >> "$LOG"
            if ws73_sle_loaded; then
                sleep 2
                continue
            fi
            radio_ok=0
            sleep 5
        done
    ) &

    echo $! > "$PIDF"
}

case "$1" in
start) start_bridge ;;
stop) stop_bridge ;;
*) echo "usage: $0 {start|stop}"; exit 1 ;;
esac
