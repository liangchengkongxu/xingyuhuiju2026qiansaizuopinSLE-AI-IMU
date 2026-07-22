#!/bin/bash
# 远程采集 SS928 板端能力快照，保存到本机 ./reports/
# 用法: bash scripts/probe_board_capability.sh

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
KIT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
# shellcheck source=scripts/board_common.sh
source "$SCRIPT_DIR/board_common.sh"

STAMP="$(date +%Y%m%d_%H%M%S)"
REPORT_DIR="$KIT_DIR/reports"
mkdir -p "$REPORT_DIR"
REMOTE="/tmp/ss928_probe_${STAMP}.txt"
LOCAL="$REPORT_DIR/ss928_probe_${BOARD_IP}_${STAMP}.txt"

echo "采集中: $BOARD_USER@$BOARD_IP → $LOCAL"

$SSH_CMD "$BOARD_USER@$BOARD_IP" "bash -s" <<'REMOTE_EOF' > "$LOCAL"
set +e
echo "======== SS928 Board Capability Probe ========"
echo "time: $(date -Is 2>/dev/null || date)"
echo "host: $(hostname)"
echo ""
echo "---- uname ----"
uname -a
echo ""
echo "---- cpu ----"
grep -E 'processor|model name|Hardware|BogoMIPS' /proc/cpuinfo 2>/dev/null | head -40
nproc 2>/dev/null
echo ""
echo "---- mem ----"
free -h 2>/dev/null || cat /proc/meminfo | head -10
echo ""
echo "---- disk ----"
df -h
echo ""
echo "---- fb / graphics ----"
ls -l /dev/fb* 2>/dev/null
for f in /sys/class/graphics/fb0/*; do
  [ -e "$f" ] || continue
  case "$(basename "$f")" in
    name|virtual_size|bits_per_pixel|stride|state) echo "$(basename "$f")=$(cat "$f" 2>/dev/null)";;
  esac
done
echo ""
echo "---- input ----"
ls -l /dev/input/event* 2>/dev/null
echo ""
echo "---- network ----"
ip -br addr 2>/dev/null || ifconfig 2>/dev/null | head -40
echo ""
echo "---- ws73 ----"
ls -l /opt/sample/ws73 2>/dev/null | head -30
lsmod 2>/dev/null | grep -E 'plat_soc|sle_soc|wifi' || true
command -v mcu_tool >/dev/null && echo "mcu_tool=yes" || echo "mcu_tool=no"
echo ""
echo "---- npu / acl libs ----"
ls -ld /opt/lib/npu /usr/lib64/npu /usr/local/Ascend 2>/dev/null
ls /opt/lib/npu 2>/dev/null | head -20
echo "LD_LIBRARY_PATH=$LD_LIBRARY_PATH"
echo ""
echo "---- qt (optional) ----"
qmake --version 2>/dev/null | head -3
ls /usr/lib/aarch64-linux-gnu/qt5/plugins/platforms 2>/dev/null | head -10
echo ""
echo "---- gcc ----"
gcc --version 2>/dev/null | head -2
echo ""
echo "---- top processes ----"
ps aux 2>/dev/null | head -25 || ps | head -25
echo ""
echo "======== end ========"
REMOTE_EOF

echo "✓ 报告已保存: $LOCAL"
echo "  可直接发给队友做离线分析。"
