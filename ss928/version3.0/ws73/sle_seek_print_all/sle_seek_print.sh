#!/bin/sh
# 板端：SLE 广播扫描打印（安装于 /opt/widget_ui/ws73）
WS73_ROOT="/opt/widget_ui/ws73"
. "$WS73_ROOT/ws73_common.sh"
bin="$WS73_ROOT/sle_seek_print_all"

usage() {
    echo "usage: $0 {0|1}"
    echo "  0  start scan print"
    echo "  1  stop"
    exit 1
}

start() {
    if [ ! -x "$bin" ]; then
        echo "missing $bin — PC: bash version2.0/scripts/deploy_ws73.sh"
        exit 1
    fi
    ws73_prep_radio
    echo "[sle_seek_print] start"
    ws73_insmod_sle || exit 1
    exec "$bin"
}

stop() {
    echo "[sle_seek_print] stop"
    killall sle_seek_print_all 2>/dev/null
    sleep 1
    ws73_rmmod_sle
}

case "$1" in
0) start ;;
1) stop ;;
*) usage ;;
esac
