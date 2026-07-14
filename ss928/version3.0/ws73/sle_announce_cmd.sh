#!/bin/sh
# 发送面板测量广播指令（短时 announce，不与 IMU 长连接同时跑）
WS73_ROOT="${WS73_ROOT:-/opt/widget_ui/ws73}"
. "$WS73_ROOT/ws73_common.sh"

BIN="$WS73_ROOT/sle_announce_cmd"

usage() {
    echo "usage: $0 start [device_id] [duration_ms]"
    echo "       $0 stop [device_id] [duration_ms]"
    exit 1
}

[ $# -ge 1 ] || usage

"$WS73_ROOT/sle_seek_bridge.sh" stop 2>/dev/null || true
"$WS73_ROOT/sle_imu_bridge.sh" stop 2>/dev/null || true
killall sle_connect_imu sle_seek_print_all sle_announce_cmd 2>/dev/null

ws73_prep_radio_light
if ! ws73_sle_loaded; then
    ws73_insmod_sle || exit 1
fi

if [ ! -x "$BIN" ]; then
    echo "missing $BIN"
    exit 1
fi

exec "$BIN" "$@"
