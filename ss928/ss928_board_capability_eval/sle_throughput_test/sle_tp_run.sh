#!/bin/sh
# 板端（SS928 发送端）：直连测速对端 20:25:05:29:15:30
ws73_ko_path=/opt/sample/ws73
client_bin=/opt/sample/ws73/sle_tp_client

usage() {
    echo "usage:"
    echo "  $0 0|start [-s bytes] [-i ms]   # 直连 20:25:05:29:15:30 发送压测"
    echo "  $0 1|stop                       # 停止并卸载 ko"
    echo "  $0 client [-s bytes] [-i ms]    # 同 start"
    exit 1
}

prep_radio() {
    killall sle_tp_client sle_connect_imu sle_seek_print_all sle_client_sample 2>/dev/null
    /opt/sample/ws73/wifi_sta.sh 1 2>/dev/null
    /opt/sample/ws73/sle_client.sh 1 2>/dev/null
    sleep 1
    rmmod sle_soc plat_soc wifi_soc 2>/dev/null
    sleep 0.5
    if command -v mcu_tool >/dev/null 2>&1; then
        mcu_tool /dev/i2c-0 0x10 nl off 2>/dev/null
        sleep 0.5
        mcu_tool /dev/i2c-0 0x10 nl on 2>/dev/null
        sleep 1
    fi
}

load_ko() {
    insmod "$ws73_ko_path/plat_soc.ko" || exit 1
    sleep 1
    insmod "$ws73_ko_path/sle_soc.ko" || exit 1
    sleep 0.5
}

run_client() {
    shift
    if [ ! -x "$client_bin" ]; then
        echo "missing $client_bin — run deploy_board.sh on PC"
        exit 1
    fi
    prep_radio
    load_ko
    echo "[sle_tp] TX -> 20:25:05:29:15:30 $*"
    exec "$client_bin" "$@"
}

stop_all() {
    echo "[sle_tp] stop"
    killall sle_tp_client 2>/dev/null
    sleep 1
    rmmod sle_soc 2>/dev/null
    rmmod plat_soc 2>/dev/null
}

case "$1" in
0|start|client) run_client "$@" ;;
1|stop) stop_all ;;
*) usage ;;
esac
