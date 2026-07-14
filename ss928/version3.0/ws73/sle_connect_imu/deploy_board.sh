#!/bin/bash
# WS73 SLE 连接 paibing_imu — 部署到板端（风格同 version1.0/scripts/deploy.sh）
# 用法: bash deploy_board.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WS73_SDK="$(cd "$SCRIPT_DIR/../../../星闪WS73原厂SDK/extracted" && pwd)"

BOARD_IP="${BOARD_IP:-192.168.1.168}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PASS="${BOARD_PASS:-ebaina}"
BOARD_INSTALL="/opt/sample/ws73"
BUILD_DIR="/tmp/sle_connect_build"

SSH_CMD="sshpass -p $BOARD_PASS ssh -o StrictHostKeyChecking=no"
SCP_CMD="sshpass -p $BOARD_PASS scp -o StrictHostKeyChecking=no"

echo "========================================"
echo "  SLE 连接 IMU - 部署到 SS928"
echo "  目标: $BOARD_USER@$BOARD_IP:$BOARD_INSTALL"
echo "========================================"

echo ""
echo "[1/3] 传输源码与 SDK 头文件/库到开发板..."
$SSH_CMD $BOARD_USER@$BOARD_IP "mkdir -p $BUILD_DIR/lib $BOARD_INSTALL" || {
    echo "错误: 无法连接开发板 $BOARD_IP"
    exit 1
}

$SCP_CMD \
    "$SCRIPT_DIR/sle_connect_main.c" \
    "$SCRIPT_DIR/sle_connect_client.c" \
    "$SCRIPT_DIR/sle_connect_client.h" \
    "$SCRIPT_DIR/Makefile.board" \
    "$SCRIPT_DIR/sle_connect.sh" \
    $BOARD_USER@$BOARD_IP:$BUILD_DIR/

$SCP_CMD "$WS73_SDK/application/lib/rk3568/sle/libsle_host.a" \
    $BOARD_USER@$BOARD_IP:$BUILD_DIR/lib/

TMP_TAR="/tmp/ws73_inc_$$.tar.gz"
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
$SSH_CMD $BOARD_USER@$BOARD_IP "bash -lc '
set -e
cd $BUILD_DIR
which gcc
gcc --version | head -1
make -f Makefile.board clean
make -f Makefile.board -j\$(nproc)
make -f Makefile.board install
test -x $BOARD_INSTALL/sle_connect_imu
ls -l $BOARD_INSTALL/sle_connect_imu
md5sum $BOARD_INSTALL/sle_connect_imu 2>/dev/null || true
'"
echo "  ✓ 编译完成"

echo ""
echo "[3/3] 安装启动脚本..."
$SCP_CMD "$SCRIPT_DIR/sle_connect.sh" $BOARD_USER@$BOARD_IP:$BOARD_INSTALL/
$SSH_CMD $BOARD_USER@$BOARD_IP "chmod +x $BOARD_INSTALL/sle_connect.sh $BOARD_INSTALL/sle_connect_imu"
echo "  ✓ 脚本已安装"

if [ "${SLE_DEPLOY_SKIP_RUN:-}" = "1" ]; then
    echo ""
    echo "  跳过自动运行 (SLE_DEPLOY_SKIP_RUN=1)"
else
    echo ""
    echo "  提示: 板端运行连接测试:"
    echo "    ssh $BOARD_USER@$BOARD_IP"
    echo "    /opt/sample/ws73/sle_connect.sh 0"
fi

echo ""
echo "========================================"
echo "  完成: $BOARD_INSTALL/sle_connect_imu"
echo "========================================"
