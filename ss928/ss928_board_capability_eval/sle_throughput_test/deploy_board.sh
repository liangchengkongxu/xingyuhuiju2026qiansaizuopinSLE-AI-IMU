#!/bin/bash
# SLE 吞吐量测试 — 远程部署到开发板（板端 native 编译）
# 用法:
#   cd board_dev_kit
#   cp board.env.example board.env   # 改 BOARD_IP
#   bash sle_throughput_test/deploy_board.sh
#
# 对端接收端: DEPLOY_SERVER=1 bash sle_throughput_test/deploy_board.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KIT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REQUIRE_WS73_SDK=1
# shellcheck source=../scripts/board_common.sh
source "$KIT_DIR/scripts/board_common.sh"

BUILD_DIR="${BOARD_BUILD_DIR}_sle_tp"
BOARD_INSTALL="$BOARD_WS73"
DEPLOY_SERVER="${DEPLOY_SERVER:-0}"

if [ "$DEPLOY_SERVER" = "1" ]; then
    ROLE_TEXT="接收端 (sle_tp_server)"
    INSTALL_CHECK="test -x $BOARD_INSTALL/sle_tp_server"
else
    ROLE_TEXT="发送端 (sle_tp_client)"
    INSTALL_CHECK="test -x $BOARD_INSTALL/sle_tp_client"
fi

echo "========================================"
echo "  SLE 吞吐量测试 - 远程部署"
echo "  角色: $ROLE_TEXT"
echo "  目标: $BOARD_USER@$BOARD_IP:$BOARD_INSTALL"
echo "  SDK:  $WS73_SDK"
echo "========================================"

echo ""
echo "[1/3] 传输源码与 SDK 到开发板..."
$SSH_CMD $BOARD_USER@$BOARD_IP "mkdir -p $BUILD_DIR/lib $BOARD_INSTALL" || {
    echo "错误: 无法连接 $BOARD_IP — 先运行 bash scripts/check_board.sh"
    exit 1
}

$SCP_CMD \
    "$SCRIPT_DIR"/*.c \
    "$SCRIPT_DIR"/*.h \
    "$SCRIPT_DIR/Makefile.board" \
    "$SCRIPT_DIR/sle_tp_run.sh" \
    $BOARD_USER@$BOARD_IP:$BUILD_DIR/

$SCP_CMD "$WS73_SDK/application/lib/rk3568/sle/libsle_host.a" \
    $BOARD_USER@$BOARD_IP:$BUILD_DIR/lib/

TMP_TAR="/tmp/ws73_inc_tp_$$.tar.gz"
tar -czf "$TMP_TAR" -C "$WS73_SDK" \
    include/bsle \
    driver/platform/libc_sec/include \
    driver/platform/drv/include \
    driver/platform/osal/include
$SCP_CMD "$TMP_TAR" $BOARD_USER@$BOARD_IP:$BUILD_DIR/ws73_inc.tgz
rm -f "$TMP_TAR"
$SSH_CMD $BOARD_USER@$BOARD_IP "cd $BUILD_DIR && tar -xzf ws73_inc.tgz && rm -f ws73_inc.tgz"
echo "  ✓ 文件传输完成"

echo ""
echo "[2/3] 在开发板上编译..."
if [ "$DEPLOY_SERVER" = "1" ]; then
    MAKE_LINE="make -f Makefile.board server && make -f Makefile.board install-server"
else
    MAKE_LINE="make -f Makefile.board client && make -f Makefile.board install-client"
fi
$SSH_CMD $BOARD_USER@$BOARD_IP "bash -lc '
set -e
cd $BUILD_DIR
make -f Makefile.board clean
$MAKE_LINE
$INSTALL_CHECK
ls -l $BOARD_INSTALL/sle_tp_*
'"
echo "  ✓ 编译完成"

echo ""
echo "[3/3] 安装启动脚本..."
if [ "$DEPLOY_SERVER" != "1" ]; then
    $SCP_CMD "$SCRIPT_DIR/sle_tp_run.sh" $BOARD_USER@$BOARD_IP:$BOARD_INSTALL/
    $SSH_CMD $BOARD_USER@$BOARD_IP "chmod +x $BOARD_INSTALL/sle_tp_run.sh $BOARD_INSTALL/sle_tp_client"
else
    $SSH_CMD $BOARD_USER@$BOARD_IP "chmod +x $BOARD_INSTALL/sle_tp_server 2>/dev/null || true"
fi
echo "  ✓ 完成"

echo ""
echo "========================================"
if [ "$DEPLOY_SERVER" = "1" ]; then
    echo "  对端: $BOARD_INSTALL/sle_tp_server -i 1000"
else
    echo "  本板: $BOARD_INSTALL/sle_tp_run.sh 0"
    echo "  停止: $BOARD_INSTALL/sle_tp_run.sh 1"
fi
echo "========================================"
