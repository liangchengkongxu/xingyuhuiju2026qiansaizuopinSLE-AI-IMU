#!/bin/bash
# SS928 Widget 面板部署脚本 — version3.0 (从 PC 交叉编译或板端编译)
# 用法: bash deploy.sh
# 仅上传二进制: WIDGET_DEPLOY_BIN_ONLY=1 bash deploy.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BOARD_IP="192.168.1.168"
BOARD_USER="root"
BOARD_PASS="ebaina"
BOARD_DIR="/opt/widget_ui"

SSH_CMD="sshpass -p $BOARD_PASS ssh -o StrictHostKeyChecking=no"
SCP_CMD="sshpass -p $BOARD_PASS scp -o StrictHostKeyChecking=no"

echo "========================================"
echo "  Widget 面板 v3.0 - 部署到 SS928"
echo "  目标: $BOARD_USER@$BOARD_IP:$BOARD_DIR"
echo "========================================"

if [ "${WIDGET_DEPLOY_BIN_ONLY:-0}" = "1" ]; then
    exec bash "$SCRIPT_DIR/deploy_bin.sh"
fi

# 1. 传输源文件到板端
echo ""
echo "[1/3] 传输文件到开发板..."
$SSH_CMD $BOARD_USER@$BOARD_IP "killall widget_panel vo_gfbg_init sample_vio_ai sample_yolov8_os08a20 2>/dev/null; sleep 1; true"
$SSH_CMD $BOARD_USER@$BOARD_IP "mkdir -p $BOARD_DIR $BOARD_DIR/bin $BOARD_DIR/models" || {
    echo "错误: 无法连接开发板 $BOARD_IP"
    echo "请确认: 1) 板子已开机 2) 网络已连接 3) SSH 已启用"
    exit 1
}
# ACL AICPU 算子默认路径 /usr/lib64/aicpu_kernels，SS928 实际在 /opt/lib/npu
echo "  配置 AICPU 算子路径..."
$SSH_CMD $BOARD_USER@$BOARD_IP "mkdir -p /usr/lib64/aicpu_kernels/0/aicpu_kernels_device && \
  ln -sf /opt/lib/npu/libcpu_kernels.so /usr/lib64/aicpu_kernels/libcpu_kernels.so && \
  ln -sf /opt/lib/npu/libaicpu_kernels.so /usr/lib64/aicpu_kernels/libaicpu_kernels.so && \
  ln -sf /opt/lib/npu/libcpu_kernels.so /usr/lib64/aicpu_kernels/0/aicpu_kernels_device/libcpu_kernels.so && \
  ln -sf /opt/lib/npu/libaicpu_kernels.so /usr/lib64/aicpu_kernels/0/aicpu_kernels_device/libaicpu_kernels.so"
$SCP_CMD "$ROOT/src/"*.cpp "$ROOT/src/"*.h "$ROOT/src/Makefile" $BOARD_USER@$BOARD_IP:$BOARD_DIR/
if [ -d "$ROOT/imucnn" ]; then
    echo "  同步 imucnn/ (1D CNN 预处理头文件)..."
    $SSH_CMD $BOARD_USER@$BOARD_IP "mkdir -p $BOARD_DIR/imucnn $BOARD_DIR/lib"
    $SCP_CMD -r "$ROOT/imucnn/"* $BOARD_USER@$BOARD_IP:$BOARD_DIR/imucnn/ 2>/dev/null || true
fi
if [[ -f "$ROOT/lib/libimu_cnn.a" ]]; then
    echo "  上传 lib/libimu_cnn.a (IMU 1D CNN NPU)..."
    $SCP_CMD "$ROOT/lib/libimu_cnn.a" $BOARD_USER@$BOARD_IP:$BOARD_DIR/lib/
fi
if [ -d "$ROOT/tutorials" ]; then
    echo "  同步教学视频到 $BOARD_DIR/tutorials ..."
    $SSH_CMD $BOARD_USER@$BOARD_IP "mkdir -p $BOARD_DIR/tutorials"
    $SCP_CMD "$ROOT/tutorials"/*.mp4 $BOARD_USER@$BOARD_IP:$BOARD_DIR/tutorials/ 2>/dev/null || true
fi
if [[ -f "$ROOT/bin/vo_gfbg_init" ]]; then
    echo "  上传 bin/vo_gfbg_init (version3.0 摄像头启动器)..."
    $SCP_CMD "$ROOT/bin/vo_gfbg_init" $BOARD_USER@$BOARD_IP:$BOARD_DIR/
fi
if [[ -f "$ROOT/bin/sample_vio_ai" ]]; then
    echo "  上传 bin/sample_vio_ai (VPSS attach 推理+画框)..."
    $SCP_CMD "$ROOT/bin/sample_vio_ai" $BOARD_USER@$BOARD_IP:$BOARD_DIR/bin/
    $SSH_CMD $BOARD_USER@$BOARD_IP "chmod +x $BOARD_DIR/bin/sample_vio_ai"
fi
# 同步运行脚本
$SCP_CMD "$ROOT/scripts/run.sh" "$ROOT/scripts/qt_env.sh" "$ROOT/scripts/detect_touch.sh" \
    "$ROOT/scripts/start_yolov8_modelzoo.sh" $BOARD_USER@$BOARD_IP:$BOARD_DIR/ 2>/dev/null || true
$SSH_CMD $BOARD_USER@$BOARD_IP "chmod +x $BOARD_DIR/start_yolov8_modelzoo.sh 2>/dev/null || true"
if [ -f "$ROOT/ws73/sle_imu_bridge.sh" ]; then
    $SCP_CMD "$ROOT/ws73/sle_imu_bridge.sh" "$ROOT/ws73/sle_seek_bridge.sh" \
        "$ROOT/ws73/ws73_boot_prep.sh" "$ROOT/ws73/ws73_common.sh" "$ROOT/ws73/wifi_sta_bridge.sh" \
        $BOARD_USER@$BOARD_IP:$BOARD_DIR/ws73/ 2>/dev/null || true
    $SSH_CMD $BOARD_USER@$BOARD_IP "chmod +x $BOARD_DIR/ws73/sle_imu_bridge.sh $BOARD_DIR/ws73/sle_seek_bridge.sh $BOARD_DIR/ws73/ws73_boot_prep.sh $BOARD_DIR/ws73/wifi_sta_bridge.sh 2>/dev/null || true"
fi
if [ -f "$ROOT/cloud.conf.example" ]; then
    $SCP_CMD "$ROOT/cloud.conf.example" $BOARD_USER@$BOARD_IP:$BOARD_DIR/
    $SSH_CMD $BOARD_USER@$BOARD_IP "test -f $BOARD_DIR/cloud.conf || cp $BOARD_DIR/cloud.conf.example $BOARD_DIR/cloud.conf"
fi
# 星闪工具（可选，设 WIDGET_DEPLOY_SKIP_WS73=1 跳过）
if [ "${WIDGET_DEPLOY_SKIP_WS73:-}" != "1" ] && [ -d "$ROOT/ws73" ]; then
    echo "  同步星闪工具到 $BOARD_DIR/ws73 ..."
    bash "$ROOT/scripts/deploy_ws73.sh"
fi
# AI 模型（可选：models/ 有实际文件时同步；设 WIDGET_DEPLOY_SKIP_MODELS=1 跳过）
if [ "${WIDGET_DEPLOY_SKIP_MODELS:-}" != "1" ] && [ -f "$ROOT/scripts/deploy_models.sh" ]; then
    has_model=0
    for f in "$ROOT/models"/*; do
        [ -e "$f" ] || continue
        base="$(basename "$f")"
        case "$base" in README.md|.gitkeep|.gitignore) continue ;; esac
        has_model=1
        break
    done
    if [ "$has_model" -eq 1 ]; then
        echo "  同步 AI 模型到 $BOARD_DIR/models ..."
        bash "$ROOT/scripts/deploy_models.sh"
    fi
fi
echo "  ✓ 文件传输完成"

# 2. 在板端编译
echo ""
echo "[2/3] 在开发板上编译..."
$SSH_CMD $BOARD_USER@$BOARD_IP "bash -lc '
set -e
cd $BOARD_DIR

# 加载 Qt 环境
source ./qt_env.sh 2>/dev/null || true

# 检查编译工具链
echo \"--- 编译环境 ---\"
which g++ 2>/dev/null && g++ --version | head -1 || echo \"错误: g++ 不可用\"
which moc 2>/dev/null && echo \"moc 可用\" || which moc-qt5 2>/dev/null && echo \"moc-qt5 可用\" || echo \"警告: moc 不可用，尝试 moc-qt5\"

# 编译 (Makefile 里处理 moc 检测)
make clean 2>/dev/null || true
if ! make -j1; then
  echo \"错误: make 失败，未生成新 widget_panel\"
  exit 1
fi
test -x ./widget_panel || { echo \"错误: 未找到可执行文件 ./widget_panel\"; exit 1; }
test -s ./widget_panel || { echo \"错误: widget_panel 为空(0字节)，请勿在编译时启动面板\"; exit 1; }
file ./widget_panel | grep -q \"ARM aarch64\" || { echo \"错误: widget_panel 不是 aarch64 可执行文件\"; file ./widget_panel; exit 1; }
echo \"编译成功\"
ls -l ./widget_panel
md5sum ./widget_panel 2>/dev/null || true
'"
echo "  ✓ 编译完成（请核对上方 ls 日期与当前板子一致）"

# 3. 启动应用（仅编译测试可设 WIDGET_DEPLOY_SKIP_RUN=1）
if [ "${WIDGET_DEPLOY_SKIP_RUN:-}" = "1" ]; then
    echo ""
    echo "[3/3] 跳过启动面板 (WIDGET_DEPLOY_SKIP_RUN=1)"
else
echo ""
echo "[3/3] 启动 Widget 面板..."
echo ""
$SSH_CMD $BOARD_USER@$BOARD_IP "bash -lc '
source /opt/widget_ui/qt_env.sh 2>/dev/null || true
cd /opt/widget_ui

# 自动探测触摸设备
if [ -f ./detect_touch.sh ]; then
    source ./detect_touch.sh
fi

bash run.sh
'" || echo "
  面板已退出。
  可手动 SSH 到板端调试：
    ssh $BOARD_USER@$BOARD_IP
    cd /opt/widget_ui
    bash run.sh
"
fi

echo ""
echo "========================================"
echo "  提示: 如需手动操作，SSH 连接板端:"
echo "  ssh $BOARD_USER@$BOARD_IP"
echo "  cd /opt/widget_ui"
echo "  bash run.sh"
echo "========================================"
