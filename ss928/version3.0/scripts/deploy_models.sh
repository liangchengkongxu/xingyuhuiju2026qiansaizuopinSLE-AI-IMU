#!/bin/bash
# version3.0 — 将 AI 模型同步到板端 /opt/widget_ui/models/
# 用法:
#   bash deploy_models.sh
#   MODEL_SRC=/path/to/models bash deploy_models.sh

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BOARD_IP="${BOARD_IP:-192.168.1.168}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PASS="${BOARD_PASS:-ebaina}"
BOARD_DIR="${BOARD_DIR:-/opt/widget_ui}"
MODEL_SRC="${MODEL_SRC:-$ROOT/models}"
BOARD_MODEL_DIR="$BOARD_DIR/models"

SSH_CMD="sshpass -p $BOARD_PASS ssh -o StrictHostKeyChecking=no"
SCP_CMD="sshpass -p $BOARD_PASS scp -o StrictHostKeyChecking=no"
RSYNC_SSH="sshpass -p $BOARD_PASS ssh -o StrictHostKeyChecking=no"

echo "========================================"
echo "  version3.0 — 部署 AI 模型"
echo "  本地: $MODEL_SRC"
echo "  板端: $BOARD_USER@$BOARD_IP:$BOARD_MODEL_DIR"
echo "========================================"

if [ ! -d "$MODEL_SRC" ]; then
    echo "错误: 模型目录不存在: $MODEL_SRC"
    exit 1
fi

# 排除 README / .gitkeep，检查是否有实际模型文件
shopt -s nullglob
model_files=("$MODEL_SRC"/*)
shopt -u nullglob
has_real=0
for f in "${model_files[@]}"; do
    base="$(basename "$f")"
    case "$base" in
        README.md|.gitkeep|.gitignore) ;;
        *) has_real=1; break ;;
    esac
done

if [ "$has_real" -eq 0 ]; then
    echo ""
    echo "提示: $MODEL_SRC 下尚无模型文件（仅有占位说明）。"
    echo "      请将 .om / 配置等放入 models/ 后重试，或设置 MODEL_SRC=..."
    echo ""
    exit 0
fi

$SSH_CMD $BOARD_USER@$BOARD_IP "mkdir -p $BOARD_MODEL_DIR" || {
    echo "错误: 无法连接开发板 $BOARD_IP"
    exit 1
}

echo ""
echo "[1/2] 同步模型文件..."
if command -v rsync >/dev/null 2>&1; then
    rsync -avz --progress \
        -e "$RSYNC_SSH" \
        --exclude 'README.md' --exclude '.gitkeep' --exclude '.gitignore' \
        "$MODEL_SRC/" "$BOARD_USER@$BOARD_IP:$BOARD_MODEL_DIR/"
else
    $SCP_CMD -r "$MODEL_SRC"/* $BOARD_USER@$BOARD_IP:$BOARD_MODEL_DIR/ 2>/dev/null || true
fi

echo ""
echo "[2/2] 板端 models 目录:"
$SSH_CMD $BOARD_USER@$BOARD_IP "ls -lh $BOARD_MODEL_DIR 2>/dev/null || true"

echo ""
echo "[3/3] 同步到 modelzoo 工作目录 (yolov8n.om)..."
$SSH_CMD $BOARD_USER@$BOARD_IP "
  om=''
  for f in $BOARD_MODEL_DIR/best_aipp_fix.om $BOARD_MODEL_DIR/*.om; do
    [ -f \"\$f\" ] || continue
    om=\"\$f\"
    break
  done
  if [ -d /opt/sample/yolov8 ] && [ -n \"\$om\" ]; then
    cp -f \"\$om\" /opt/sample/yolov8/yolov8n.om
    ls -lh /opt/sample/yolov8/yolov8n.om
  else
    echo '  跳过: /opt/sample/yolov8 或 models/*.om 不存在'
  fi
"

echo ""
echo "✓ 模型部署完成"
echo "  板端路径: $BOARD_MODEL_DIR"
echo "  modelzoo: /opt/sample/yolov8/yolov8n.om"
