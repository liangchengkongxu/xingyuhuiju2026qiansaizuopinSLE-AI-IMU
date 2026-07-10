#!/bin/bash
# 仅部署 AI/摄像头二进制 + run.sh，跳过板端 widget_panel 全量编译（省时间）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BOARD_IP="${BOARD_IP:-192.168.1.168}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PASS="${BOARD_PASS:-ebaina}"
BOARD_DIR="${BOARD_DIR:-/opt/widget_ui}"

SSH_CMD="sshpass -p $BOARD_PASS ssh -o StrictHostKeyChecking=no"
SCP_CMD="sshpass -p $BOARD_PASS scp -o StrictHostKeyChecking=no"

echo "=== 快速部署 bin (跳过 panel 编译) ==="

for f in "$ROOT/bin/vo_gfbg_init" "$ROOT/bin/sample_vio_ai"; do
  if [[ ! -f "$f" ]]; then
    echo "错误: 缺少 $f，请先运行 build_vo_gfbg_init.sh / build_vio_ai.sh"
    exit 1
  fi
done

$SSH_CMD $BOARD_USER@$BOARD_IP "killall widget_panel vo_gfbg_init sample_vio_ai 2>/dev/null; sleep 1; true"
$SCP_CMD "$ROOT/bin/vo_gfbg_init" $BOARD_USER@$BOARD_IP:$BOARD_DIR/
$SCP_CMD "$ROOT/bin/sample_vio_ai" $BOARD_USER@$BOARD_IP:$BOARD_DIR/bin/
$SCP_CMD "$ROOT/scripts/run.sh" $BOARD_USER@$BOARD_IP:$BOARD_DIR/
$SSH_CMD $BOARD_USER@$BOARD_IP "chmod +x $BOARD_DIR/vo_gfbg_init $BOARD_DIR/bin/sample_vio_ai $BOARD_DIR/run.sh"

echo "✓ 已上传 vo_gfbg_init + sample_vio_ai + run.sh"
echo "  板端: cd $BOARD_DIR && bash run.sh"
