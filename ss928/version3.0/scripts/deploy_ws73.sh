#!/bin/bash
# 星闪工具部署到板端 /opt/widget_ui/ws73（与 Widget 面板同目录）
# 用法: bash scripts/deploy_ws73.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
WS73_DIR="$ROOT/ws73"
SDK="$(cd "$ROOT/../星闪WS73原厂SDK/extracted" && pwd)"

BOARD_IP="${BOARD_IP:-192.168.1.168}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PASS="${BOARD_PASS:-ebaina}"
BOARD_WS73="/opt/widget_ui/ws73"

SSH_CMD="sshpass -p $BOARD_PASS ssh -o StrictHostKeyChecking=no"
SCP_CMD="sshpass -p $BOARD_PASS scp -o StrictHostKeyChecking=no"

pack_headers() {
    local out="$1"
    tar -czf "$out" -C "$SDK" \
        include/bsle \
        driver/platform/libc_sec/include \
        driver/platform/drv/include \
        driver/platform/osal/include
}

build_one() {
    local name="$1"
    local srcdir="$WS73_DIR/$name"
    local build_dir="/tmp/ws73_build_${name}"

    echo ""
    echo "--- 编译 $name ---"
    $SSH_CMD $BOARD_USER@$BOARD_IP "rm -rf $build_dir && mkdir -p $build_dir/lib"
    $SCP_CMD "$srcdir"/*.c "$srcdir"/*.h "$srcdir/Makefile.board" \
        $BOARD_USER@$BOARD_IP:$build_dir/
    $SCP_CMD "$SDK/application/lib/rk3568/sle/libsle_host.a" \
        $BOARD_USER@$BOARD_IP:$build_dir/lib/
    local tgz="/tmp/ws73_hdr_$$.tar.gz"
    pack_headers "$tgz"
    $SCP_CMD "$tgz" $BOARD_USER@$BOARD_IP:$build_dir/ws73_inc.tgz
    rm -f "$tgz"
    $SSH_CMD $BOARD_USER@$BOARD_IP "cd $build_dir && tar -xzf ws73_inc.tgz && rm -f ws73_inc.tgz && \
        make -f Makefile.board WS73_INC=$build_dir LIBDIR=$build_dir/lib clean all install"
}

echo "========================================"
echo "  星闪 WS73 -> $BOARD_USER@$BOARD_IP:$BOARD_WS73"
echo "========================================"

echo ""
echo "[1/4] 创建目录并同步脚本..."
$SSH_CMD $BOARD_USER@$BOARD_IP "mkdir -p $BOARD_WS73/ko"
$SCP_CMD "$WS73_DIR/ws73_common.sh" \
    "$WS73_DIR/sle_seek_print_all/sle_seek_print.sh" \
    "$WS73_DIR/sle_imu_bridge.sh" \
    "$WS73_DIR/sle_seek_bridge.sh" \
    "$WS73_DIR/ws73_boot_prep.sh" \
    "$WS73_DIR/imu_speed_demo.py" \
    "$WS73_DIR/run_imu_speed_demo.sh" \
    $BOARD_USER@$BOARD_IP:$BOARD_WS73/
$SSH_CMD $BOARD_USER@$BOARD_IP "chmod +x $BOARD_WS73/*.sh $BOARD_WS73/imu_speed_demo.py"

echo ""
echo "[2/4] 准备驱动 ko（从板端原厂 sample 拷到 widget_ui，仅首次或缺失时）..."
$SSH_CMD $BOARD_USER@$BOARD_IP "bash -lc '
set -e
mkdir -p $BOARD_WS73/ko
if [ -f /opt/sample/ws73/plat_soc.ko ]; then
  cp -f /opt/sample/ws73/plat_soc.ko /opt/sample/ws73/sle_soc.ko $BOARD_WS73/ko/
  [ -f /opt/sample/ws73/wifi_soc.ko ] && cp -f /opt/sample/ws73/wifi_soc.ko $BOARD_WS73/ko/ || true
fi
ls -l $BOARD_WS73/ko/
'"

echo ""
echo "[3/4] 板端编译 sle_seek_print_all（扫描 + 广播 IMU）..."
build_one sle_seek_print_all

echo ""
echo "[4/4] 校验安装..."
$SSH_CMD $BOARD_USER@$BOARD_IP "ls -l $BOARD_WS73/sle_seek_print_all $BOARD_WS73/*.sh"

echo ""
echo "========================================"
echo "  完成。板端命令:"
echo "  设备扫描: $BOARD_WS73/sle_seek_bridge.sh start"
echo "  IMU 广播: $BOARD_WS73/sle_imu_bridge.sh start && tail -f /tmp/sle_imu_lines"
echo "========================================"
