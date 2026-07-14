#!/bin/bash
# 在 SS928 板端安装 widget_ui 开机自启（systemd）
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BOARD_IP="${BOARD_IP:-192.168.1.168}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PASS="${BOARD_PASS:-ebaina}"
SERVICE_NAME="widget_ui.service"
REMOTE_UNIT="/etc/systemd/system/${SERVICE_NAME}"

SSH_CMD="sshpass -p ${BOARD_PASS} ssh -o StrictHostKeyChecking=no ${BOARD_USER}@${BOARD_IP}"
SCP_CMD="sshpass -p ${BOARD_PASS} scp -o StrictHostKeyChecking=no"

echo "========================================"
echo "  Widget 面板开机自启 → ${BOARD_USER}@${BOARD_IP}"
echo "========================================"

${SSH_CMD} "test -x /opt/widget_ui/run.sh" || {
    echo "错误: 板端未找到 /opt/widget_ui/run.sh，请先 bash scripts/deploy.sh"
    exit 1
}

echo "[1/5] 写入 systemd 单元（避免 scp 损坏，用 ssh 直写）..."
${SSH_CMD} "cat > ${REMOTE_UNIT}" < "${SCRIPT_DIR}/widget_ui.service"
${SSH_CMD} "chmod 644 ${REMOTE_UNIT} && wc -c ${REMOTE_UNIT} && head -3 ${REMOTE_UNIT}"

echo "[2/5] 校验 unit 文件..."
${SSH_CMD} "systemd-analyze verify ${REMOTE_UNIT} 2>&1 | grep -E 'widget_ui|error' || true"

echo "[3/5] 停用旧界面服务（kiosk / ui-server）..."
${SSH_CMD} "systemctl disable kiosk.service ui-server.service 2>/dev/null || true; systemctl stop kiosk.service ui-server.service 2>/dev/null || true"

echo "[4/5] 启用 ${SERVICE_NAME}..."
${SSH_CMD} "systemctl daemon-reload && systemctl enable ${SERVICE_NAME}"

echo "[5/5] 状态..."
${SSH_CMD} "systemctl is-enabled ${SERVICE_NAME}; systemctl is-active ${SERVICE_NAME} 2>/dev/null || true"

echo ""
echo "完成。重启后将自动运行 /opt/widget_ui/run.sh"
echo "  日志: tail -f /tmp/widget_ui_boot.log"
echo "  状态: systemctl status ${SERVICE_NAME}"

if [ "${INSTALL_AUTOSTART_START:-0}" = "1" ]; then
    echo ""
    echo "立即启动 ${SERVICE_NAME} ..."
    ${SSH_CMD} "systemctl restart ${SERVICE_NAME} 2>/dev/null || systemctl start ${SERVICE_NAME}"
    sleep 12
    ${SSH_CMD} "systemctl status ${SERVICE_NAME} --no-pager -l 2>&1 | head -20; tail -15 /tmp/widget_ui_boot.log 2>/dev/null || true"
fi
