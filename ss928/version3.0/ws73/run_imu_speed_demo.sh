#!/bin/sh
# 板端：连接 paibing_imu 并实时估算球速（终端可见 SLE 日志 + Python 算速）
WS73_ROOT="/opt/widget_ui/ws73"
PY="$WS73_ROOT/imu_speed_demo.py"
CONNECT="$WS73_ROOT/sle_connect.sh"
FIFO="/tmp/ws73_imu_fifo"

if [ ! -f "$PY" ]; then
    echo "缺少 $PY，请在 PC 执行: bash version2.0/scripts/deploy_ws73.sh"
    exit 1
fi

cleanup() {
    rm -f "$FIFO"
    killall sle_connect_imu 2>/dev/null
}
trap cleanup EXIT INT TERM

echo "[demo] 启动星闪连接 + 球速估算（Ctrl+C 结束）"
echo "[demo] 若无输出：确认 paibing_imu 已开机；或先 mcu_tool nl on"
echo "[demo] 仅测算法: python3 $PY --demo"
echo ""

rm -f "$FIFO"
mkfifo "$FIFO" || { echo "mkfifo 失败"; exit 1; }

python3 -u "$PY" --no-echo-sle < "$FIFO" &
PY_PID=$!

# tee：一份给终端显示，一份给 python 解析（避免管道吞掉 SLE 日志）
"$CONNECT" 0 2>&1 | tee "$FIFO"
CONNECT_RC=$?

wait "$PY_PID" 2>/dev/null
exit "$CONNECT_RC"
