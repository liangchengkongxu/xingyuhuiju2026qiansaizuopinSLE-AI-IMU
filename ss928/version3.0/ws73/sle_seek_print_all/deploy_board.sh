#!/bin/bash
set -euo pipefail

BOARD="${BOARD:-root@192.168.1.168}"
PASS="${PASS:-ebaina}"
HERE="$(cd "$(dirname "$0")" && pwd)"
WS73_SDK="$(cd "$HERE/../../../星闪WS73原厂SDK/extracted" && pwd)"
BUILD_DIR="/tmp/sle_seek_build"

SSH="sshpass -p $PASS ssh -o StrictHostKeyChecking=no $BOARD"
SCP="sshpass -p $PASS scp -o StrictHostKeyChecking=no"

echo "==> prepare $BUILD_DIR on board"
$SSH "rm -rf $BUILD_DIR && mkdir -p $BUILD_DIR/lib"

echo "==> copy sources"
$SCP "$HERE/sle_seek_print_main.c" "$HERE/sle_seek_print_client.c" "$HERE/sle_seek_print_client.h" \
    "$HERE/Makefile.board" "$HERE/sle_seek_print.sh" \
    "$BOARD:$BUILD_DIR/"

echo "==> copy libsle_host.a"
$SCP "$WS73_SDK/application/lib/rk3568/sle/libsle_host.a" "$BOARD:$BUILD_DIR/lib/"

echo "==> copy headers (tar)"
TMP_TAR="/tmp/ws73_inc_$$.tar.gz"
tar -czf "$TMP_TAR" -C "$WS73_SDK" \
    include/bsle \
    driver/platform/libc_sec/include \
    driver/platform/drv/include \
    driver/platform/osal/include
$SCP "$TMP_TAR" "$BOARD:$BUILD_DIR/ws73_inc.tgz"
rm -f "$TMP_TAR"
$SSH "cd $BUILD_DIR && tar -xzf ws73_inc.tgz && rm ws73_inc.tgz"

echo "==> build on board"
$SSH "cd $BUILD_DIR && make -f Makefile.board clean all install"
$SCP "$HERE/sle_seek_print.sh" "$BOARD:/opt/sample/ws73/"
$SSH "chmod +x /opt/sample/ws73/sle_seek_print.sh /opt/sample/ws73/sle_seek_print_all"

echo "==> done: /opt/sample/ws73/sle_seek_print_all"
