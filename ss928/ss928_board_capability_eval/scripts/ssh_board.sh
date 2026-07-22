#!/bin/bash
# 快速 SSH 登录开发板
# 用法: bash scripts/ssh_board.sh [远程命令...]

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/board_common.sh
source "$SCRIPT_DIR/board_common.sh"

echo "连接 $BOARD_USER@$BOARD_IP ..."
if [ $# -gt 0 ]; then
    $SSH_CMD "$BOARD_USER@$BOARD_IP" "$@"
else
    exec $SSH_CMD "$BOARD_USER@$BOARD_IP"
fi
