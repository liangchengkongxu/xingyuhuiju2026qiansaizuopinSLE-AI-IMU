#!/bin/sh
# WiFi STA：只做 insmod + wpa，不重载驱动
WS73_ROOT="${WS73_ROOT:-/opt/widget_ui/ws73}"
. "$WS73_ROOT/ws73_common.sh"

LOG="/tmp/wifi_sta_bridge.log"
WATCH_PIDF="/tmp/wifi_sta_watch.pid"

log() {
    echo "$(date '+%F %T') $*" >> "$LOG"
}

wifi_status() {
    if ws73_wifi_loaded; then
        echo "wifi_soc: loaded"
    else
        echo "wifi_soc: not loaded"
    fi
    if ws73_sle_loaded; then
        echo "sle_soc: loaded"
    else
        echo "sle_soc: not loaded"
    fi
    if ip link show wlan0 >/dev/null 2>&1; then
        ip link show wlan0 2>/dev/null | head -1
        ip -4 addr show dev wlan0 2>/dev/null | awk '/inet /{print "wlan0:", $2}'
    else
        echo "wlan0: missing"
    fi
    if pgrep -x wpa_supplicant >/dev/null 2>&1; then
        echo "wpa_supplicant: running"
    else
        echo "wpa_supplicant: stopped"
    fi
}

wifi_bringup_once() {
    log "wifi bringup (insmod only, no rmmod)"
    ws73_wifi_bringup
}

wifi_watch_loop() {
    while true; do
        sleep "${WIDGET_WIFI_WATCH_SEC:-60}"
        if ip link show wlan0 >/dev/null 2>&1; then
            ws73_wifi_userspace_fix >> "$LOG" 2>&1 || true
        elif ws73_wifi_wedged >> "$LOG" 2>&1; then
            : # wedged: 只打日志，不重试驱动
        fi
    done
}

case "$1" in
start)
    log "wifi start"
    if wifi_bringup_once; then
        log "wifi start ok"
        wifi_status >> "$LOG"
        exit 0
    fi
    log "wifi start failed"
    wifi_status >> "$LOG"
    exit 1
    ;;
watch)
    if [ -f "$WATCH_PIDF" ] && kill -0 "$(cat "$WATCH_PIDF")" 2>/dev/null; then
        exit 0
    fi
    echo $$ > "$WATCH_PIDF"
    log "wifi watch pid=$$"
    wifi_watch_loop
    ;;
autostart)
    log "wifi autostart"
    wifi_bringup_once || true
    wifi_status >> "$LOG"
    if [ -f "$WATCH_PIDF" ] && kill -0 "$(cat "$WATCH_PIDF")" 2>/dev/null; then
        exit 0
    fi
    wifi_watch_loop &
    echo $! > "$WATCH_PIDF"
    wait "$(cat "$WATCH_PIDF")"
    ;;
stop)
    log "wifi stop"
    if [ -f "$WATCH_PIDF" ]; then
        kill "$(cat "$WATCH_PIDF")" 2>/dev/null
        rm -f "$WATCH_PIDF"
    fi
    ws73_wifi_stop
    exit 0
    ;;
status)
    wifi_status
    ;;
ensure)
    log "wifi ensure"
    ws73_wifi_ensure
    wifi_status >> "$LOG"
    ;;
*)
    echo "usage: $0 {start|stop|status|ensure|watch|autostart}"
    exit 1
    ;;
esac
