#!/bin/sh
# 星闪模组公共路径（与 version2.0 面板同目录 /opt/widget_ui/ws73）
WS73_ROOT="${WS73_ROOT:-/opt/widget_ui/ws73}"
WS73_KO="${WS73_KO:-$WS73_ROOT/ko}"
LEGACY_SAMPLE="/opt/sample/ws73"

ws73_ensure_ko() {
    mkdir -p "$WS73_KO"
    if [ -f "$WS73_KO/plat_soc.ko" ] && [ -f "$WS73_KO/sle_soc.ko" ]; then
        return 0
    fi
    if [ -f "$LEGACY_SAMPLE/plat_soc.ko" ]; then
        echo "[ws73] 从 $LEGACY_SAMPLE 复制驱动到 $WS73_KO"
        cp -f "$LEGACY_SAMPLE/plat_soc.ko" "$LEGACY_SAMPLE/sle_soc.ko" "$WS73_KO/" 2>/dev/null || true
        [ -f "$LEGACY_SAMPLE/wifi_soc.ko" ] && cp -f "$LEGACY_SAMPLE/wifi_soc.ko" "$WS73_KO/" 2>/dev/null || true
    fi
    if [ ! -f "$WS73_KO/plat_soc.ko" ]; then
        echo "[ws73] 错误: 缺少 $WS73_KO/plat_soc.ko，请部署或从原厂 sample 拷贝"
        return 1
    fi
    return 0
}

ws73_sle_loaded() {
    lsmod 2>/dev/null | grep -q sle_soc
}

ws73_wifi_loaded() {
    lsmod 2>/dev/null | grep -q wifi_soc
}

ws73_udhcpc_wlan0_stop() {
    if [ -f /var/run/udhcpc.wlan0.pid ]; then
        kill "$(cat /var/run/udhcpc.wlan0.pid)" 2>/dev/null
    fi
    for pid in $(pidof udhcpc 2>/dev/null); do
        if ps -o args= -p "$pid" 2>/dev/null | grep -q -- '-i wlan0'; then
            kill "$pid" 2>/dev/null
        fi
    done
}

ws73_wait_wlan0() {
    i=0
    while [ "$i" -lt "${1:-20}" ]; do
        ip link show wlan0 >/dev/null 2>&1 && return 0
        sleep 1
        i=$((i + 1))
    done
    return 1
}

# 仅 wpa + dhcp，不碰 ko
ws73_wifi_userspace_fix() {
    if ! ip link show wlan0 >/dev/null 2>&1; then
        return 1
    fi
    if ! pgrep -x wpa_supplicant >/dev/null 2>&1; then
        wpa_supplicant -iwlan0 -Dnl80211 \
            -c"${WIDGET_WIFI_WPA_CONF:-/etc/wireless/wpa_supplicant.conf}" -B 2>/dev/null
        sleep 1
    fi
    if ! ip -4 addr show dev wlan0 2>/dev/null | grep -q "inet "; then
        ws73_udhcpc_wlan0_stop
        udhcpc -i wlan0 -b -q -t "${WIDGET_WIFI_DHCP_TRIES:-8}" 2>/dev/null
    fi
    if [ "${WIDGET_WIFI_BOOT_FAST:-0}" = "1" ]; then
        ip link show wlan0 >/dev/null 2>&1
        return $?
    fi
    ip -4 addr show dev wlan0 2>/dev/null | grep -q "inet "
}

# 驱动 wedged：已 insmod 但无 wlan0 — 绝不 rmmod，只能断电重启
ws73_wifi_wedged() {
    if ws73_wifi_loaded && ! ip link show wlan0 >/dev/null 2>&1; then
        echo "[ws73] wifi_soc 已加载但 wlan0 不存在 — 请断电重启（脚本不会 rmmod 驱动）" >&2
        return 0
    fi
    if ws73_sle_loaded && ! ip link show wlan0 >/dev/null 2>&1; then
        echo "[ws73] sle_soc 先于 WiFi 加载 — 请断电重启（开机 widget_wifi 会先起 WiFi）" >&2
        return 0
    fi
    return 1
}

ws73_wait_module() {
    mod="$1"
    limit="${2:-20}"
    i=0
    while [ "$i" -lt "$limit" ]; do
        lsmod 2>/dev/null | grep -q "$mod" && return 0
        sleep 1
        i=$((i + 1))
    done
    return 1
}

# 板端 /etc/init.d/S92wifi 已在 rc.local 里 mcu 复位 + wifi_sta.sh 0
ws73_board_wifi_bootstrapped() {
    [ -f /etc/init.d/S92wifi ]
}

# 直接调用原厂 wifi_sta.sh 0（仅 insmod，无 rmmod）
ws73_legacy_wifi_sta_start() {
    if [ ! -x "$LEGACY_SAMPLE/wifi_sta.sh" ]; then
        return 1
    fi
    echo "[ws73] 调用 $LEGACY_SAMPLE/wifi_sta.sh 0" >&2
    if command -v mcu_tool >/dev/null 2>&1; then
        mcu_tool /dev/i2c-0 0x10 nl off 2>/dev/null
        sleep 0.5
        mcu_tool /dev/i2c-0 0x10 nl on 2>/dev/null
        sleep 1
    fi
    "$LEGACY_SAMPLE/wifi_sta.sh" 0
    sleep 3
}

# WiFi 启动：不重载驱动；优先等 S92wifi，否则走原厂 wifi_sta.sh 0
ws73_wifi_bringup() {
    local fast=0
    local wlan_wait=25
    local wlan_wait2=30
    ws73_ensure_ko || return 1

    if ws73_wifi_wedged; then
        return 1
    fi

    if [ "${WIDGET_WIFI_BOOT_FAST:-0}" = "1" ]; then
        fast=1
        wlan_wait=5
        wlan_wait2=8
        if ip link show wlan0 >/dev/null 2>&1; then
            if ! pgrep -x wpa_supplicant >/dev/null 2>&1; then
                wpa_supplicant -iwlan0 -Dnl80211 \
                    -c"${WIDGET_WIFI_WPA_CONF:-/etc/wireless/wpa_supplicant.conf}" -B 2>/dev/null
            fi
            ( udhcpc -i wlan0 -b -q -t "${WIDGET_WIFI_DHCP_TRIES:-3}" 2>/dev/null & )
            return 0
        fi
    fi

    if ws73_board_wifi_bootstrapped; then
        if [ "$fast" = 1 ]; then
            ws73_wait_module wifi_soc 3 || true
        else
            ws73_wait_module wifi_soc 8 || true
        fi
    fi

    if ! ws73_wifi_loaded && ! ip link show wlan0 >/dev/null 2>&1; then
        ws73_legacy_wifi_sta_start || return 1
    fi

    if ws73_wifi_loaded && ! ip link show wlan0 >/dev/null 2>&1; then
        if ! ws73_wait_wlan0 "$wlan_wait"; then
            ws73_wifi_wedged
            return 1
        fi
    fi

    if ! ip link show wlan0 >/dev/null 2>&1; then
        if ! ws73_wait_wlan0 "$wlan_wait2"; then
            ws73_wifi_wedged
            return 1
        fi
    fi

    if [ "$fast" = 1 ]; then
        ws73_wifi_userspace_fix || true
        return 0
    fi

    ws73_wifi_userspace_fix
}

ws73_wifi_start() {
    ws73_wifi_bringup
}

# 看门狗：只修 wpa/dhcp；驱动异常时不重试 insmod
ws73_wifi_ensure() {
    [ "${WIDGET_WIFI_ENABLE:-1}" = "1" ] || return 0
    if ip link show wlan0 >/dev/null 2>&1 \
        && pgrep -x wpa_supplicant >/dev/null 2>&1 \
        && ip -4 addr show dev wlan0 2>/dev/null | grep -q "inet "; then
        return 0
    fi
    if ws73_wifi_wedged; then
        return 1
    fi
    if ip link show wlan0 >/dev/null 2>&1; then
        ws73_wifi_userspace_fix
        return $?
    fi
    ws73_wifi_bringup
}

ws73_wifi_stop() {
    killall wpa_supplicant 2>/dev/null
    ws73_udhcpc_wlan0_stop
}

# WiFi 就绪后再 insmod sle（不重载任何驱动）
ws73_coexist_bringup() {
    if ! ip link show wlan0 >/dev/null 2>&1; then
        ws73_wifi_bringup || return 1
    else
        ws73_wifi_userspace_fix || true
    fi
    if ! ws73_sle_loaded; then
        ws73_ensure_ko || return 1
        if ! lsmod 2>/dev/null | grep -q plat_soc; then
            insmod "$WS73_KO/plat_soc.ko" || return 1
            sleep 1
        fi
        insmod "$WS73_KO/sle_soc.ko" || return 1
        sleep 0.5
    fi
    return 0
}

ws73_prep_radio_light() {
    killall sle_connect_imu sle_seek_print_all sle_client_sample sle_announce_cmd 2>/dev/null
}

ws73_prep_radio_imu() {
    ws73_prep_radio_light
    if [ "${WIDGET_WS73_KEEP_WIFI:-1}" = "1" ]; then
        return 0
    fi
    if ws73_sle_loaded; then
        return 0
    fi
    if command -v mcu_tool >/dev/null 2>&1; then
        mcu_tool /dev/i2c-0 0x10 nl off 2>/dev/null
        sleep 0.5
        mcu_tool /dev/i2c-0 0x10 nl on 2>/dev/null
        sleep 1
    fi
}

ws73_prep_radio() {
    ws73_prep_radio_light
    if [ "${WIDGET_WS73_KEEP_WIFI:-1}" = "1" ]; then
        return 0
    fi
    if [ "${WIDGET_WS73_SKIP_WIFI:-1}" = "0" ] && [ -x "$LEGACY_SAMPLE/wifi_sta.sh" ]; then
        "$LEGACY_SAMPLE/wifi_sta.sh" 1 2>/dev/null
    fi
    if [ "${WIDGET_WS73_SKIP_LEGACY_CLIENT:-1}" = "0" ] && [ -x "$LEGACY_SAMPLE/sle_client.sh" ]; then
        "$LEGACY_SAMPLE/sle_client.sh" 1 2>/dev/null
    fi
    sleep 1
    if command -v mcu_tool >/dev/null 2>&1; then
        mcu_tool /dev/i2c-0 0x10 nl off 2>/dev/null
        sleep 0.5
        mcu_tool /dev/i2c-0 0x10 nl on 2>/dev/null
        sleep 1
    fi
}

ws73_insmod_sle() {
    ws73_ensure_ko || return 1
    if ws73_sle_loaded; then
        return 0
    fi
    ws73_apply_bsle_max_rx_ini
    if [ "${WIDGET_WS73_KEEP_WIFI:-1}" = "1" ] && ! ip link show wlan0 >/dev/null 2>&1; then
        echo "[ws73] wlan0 未就绪，暂不加载 sle_soc" >&2
        return 1
    fi
    if ! lsmod 2>/dev/null | grep -q plat_soc; then
        insmod "$WS73_KO/plat_soc.ko" || return 1
        sleep 1
    fi
    insmod "$WS73_KO/sle_soc.ko" || return 1
    sleep 0.5
    return 0
}

ws73_rmmod_sle() {
    [ "${WIDGET_WS73_ALLOW_RMMOD:-0}" = "1" ] || return 0
    rmmod sle_soc 2>/dev/null
    rmmod plat_soc 2>/dev/null
}

# 星闪最大接收：提高 BSLE 相对 WiFi 优先级（需下次 insmod 前生效）
ws73_apply_bsle_max_rx_ini() {
    [ "${WIDGET_WS73_BSLE_MAX_COEX:-0}" = "1" ] || return 0
    local ini f
    for f in /etc/wireless/ws73_cfg.ini /etc/wireless/ws73_cfg_default.ini \
        /opt/sample/ws73/ws73_cfg.ini /opt/sample/ws73/ws73_cfg_default.ini \
        /etc/wifi/ws73_cfg.ini; do
        [ -f "$f" ] || continue
        ini="$f"
        break
    done
    [ -n "$ini" ] || return 0
    if grep -q '^bsle_coex_param=' "$ini" 2>/dev/null; then
        sed -i 's/^bsle_coex_param=.*/bsle_coex_param=2/' "$ini"
    fi
    if grep -q '^bt_coex_mode=' "$ini" 2>/dev/null; then
        sed -i 's/^bt_coex_mode=.*/bt_coex_mode=9/' "$ini"
    fi
    if grep -q '^bt_maxpower=' "$ini" 2>/dev/null; then
        sed -i 's/^bt_maxpower=.*/bt_maxpower=7/' "$ini"
    fi
    echo "[ws73] BSLE max RX ini: $ini (coex=2 bt_coex_mode=9 maxpower=7)" >&2
}
