#!/bin/bash
# 检查 PC 到开发板的网络与 SSH（不强制 WS73 SDK）
# 用法: bash scripts/check_board.sh

set -e
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=scripts/board_common.sh
source "$SCRIPT_DIR/board_common.sh"

echo "========================================"
echo "  SS928 开发板连通性检查"
echo "  目标: $BOARD_USER@$BOARD_IP"
echo "========================================"

echo ""
echo "[1] ping ..."
if ping -c 2 -W 2 "$BOARD_IP" >/dev/null 2>&1; then
    echo "  ✓ $BOARD_IP 可达"
else
    echo "  ✗ ping 失败 — 确认板子开机、网线、PC 与板子同网段"
    exit 1
fi

echo ""
echo "[2] SSH ..."
if $SSH_CMD "$BOARD_USER@$BOARD_IP" "echo ok && uname -m && hostname"; then
    echo "  ✓ SSH 正常"
else
    echo "  ✗ SSH 失败 — 检查账号密码或 board.env"
    exit 1
fi

echo ""
echo "[3] 板端基础环境 ..."
$SSH_CMD "$BOARD_USER@$BOARD_IP" "bash -lc '
echo \"  kernel: \$(uname -r)\"
echo \"  arch:   \$(uname -m)\"
df -h / | tail -1 | awk \"{print \\\"  disk:   \\\" \\\$3 \\\" used / \\\" \\\$2 \\\" (\\\" \\\$5 \\\")\\\"}\"
if command -v gcc >/dev/null 2>&1; then gcc --version | head -1 | sed \"s/^/  gcc:    /\"; else echo \"  gcc:    未找到\"; fi
if [ -c /dev/fb0 ]; then echo \"  fb0:    存在\"; else echo \"  fb0:    无\"; fi
if [ -d $BOARD_WS73 ]; then
  echo \"  ws73:   $BOARD_WS73 存在\"
  ls $BOARD_WS73/plat_soc.ko $BOARD_WS73/sle_soc.ko 2>/dev/null | sed \"s|^|         |\" || echo \"         (缺 ko)\"
else
  echo \"  ws73:   无 $BOARD_WS73\"
fi
if command -v mcu_tool >/dev/null 2>&1; then echo \"  mcu_tool: 可用\"; else echo \"  mcu_tool: 未找到\"; fi
'"

echo ""
echo "[4] PC 侧 WS73 SDK（测速才需要）..."
if [ -n "${WS73_SDK:-}" ] && [ -f "$WS73_SDK/application/lib/rk3568/sle/libsle_host.a" ]; then
    echo "  ✓ $WS73_SDK"
else
    echo "  - 未配置（仅影响 sle_throughput_test 部署）"
fi

echo ""
echo "连通检查通过。下一步:"
echo "  bash scripts/probe_board_capability.sh"
echo "  bash sle_throughput_test/deploy_board.sh   # 需 WS73 SDK"
