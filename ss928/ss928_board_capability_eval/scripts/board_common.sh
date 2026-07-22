#!/bin/bash
# 板端公共环境 — 被 deploy / ssh / probe 脚本 source
# REQUIRE_WS73_SDK=1 时才强制检查 libsle_host.a（测速部署用）

KIT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if [ -f "$KIT_DIR/board.env" ]; then
    # shellcheck source=/dev/null
    source "$KIT_DIR/board.env"
fi

BOARD_IP="${BOARD_IP:-192.168.1.168}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PASS="${BOARD_PASS:-ebaina}"
BOARD_WS73="${BOARD_WS73:-/opt/sample/ws73}"
BOARD_BUILD_DIR="${BOARD_BUILD_DIR:-/tmp/ws73_build}"

resolve_ws73_sdk() {
    if [ -n "${WS73_SDK:-}" ] && [ -f "$WS73_SDK/application/lib/rk3568/sle/libsle_host.a" ]; then
        return 0
    fi
    WS73_SDK=""
    local candidate
    for candidate in \
        "$KIT_DIR/../星闪WS73原厂SDK/extracted" \
        "$KIT_DIR/ws73_sdk/extracted" \
        "/opt/ws73_sdk/extracted"; do
        if [ -f "$candidate/application/lib/rk3568/sle/libsle_host.a" ]; then
            WS73_SDK="$candidate"
            return 0
        fi
    done
    return 1
}

resolve_ws73_sdk || true

if [ "${REQUIRE_WS73_SDK:-0}" = "1" ]; then
    if [ -z "${WS73_SDK:-}" ] || [ ! -f "$WS73_SDK/application/lib/rk3568/sle/libsle_host.a" ]; then
        echo "错误: 未找到 WS73 SDK (libsle_host.a)"
        echo "请设置 WS73_SDK 或解压到:"
        echo "  $KIT_DIR/ws73_sdk/extracted"
        return 1 2>/dev/null || exit 1
    fi
fi

export BOARD_IP BOARD_USER BOARD_PASS BOARD_WS73 BOARD_BUILD_DIR WS73_SDK

if ! command -v sshpass >/dev/null 2>&1; then
    echo "错误: 需要 sshpass — sudo apt install sshpass"
    return 1 2>/dev/null || exit 1
fi

SSH_CMD="sshpass -p $BOARD_PASS ssh -o StrictHostKeyChecking=no -o ConnectTimeout=8"
SCP_CMD="sshpass -p $BOARD_PASS scp -o StrictHostKeyChecking=no"
