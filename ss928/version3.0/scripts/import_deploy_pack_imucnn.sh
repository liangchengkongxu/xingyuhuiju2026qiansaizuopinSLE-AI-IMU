#!/bin/bash
# 将 gongxiang/deploy（1D CNN 九轴击球模型）导入 version3.0
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PACK_DIR="${1:-${DEPLOY_PACK_IMUCNN:-/mnt/hgfs/gongxiang/deploy}}"
IMU_DIR="$ROOT/imucnn"
MODEL_DIR="$ROOT/models"

echo "========================================"
echo "  导入 IMU 1D CNN -> imucnn/ + models/"
echo "  源: $PACK_DIR"
echo "========================================"

if [[ ! -d "$PACK_DIR" ]]; then
    echo "错误: 目录不存在: $PACK_DIR"
    echo "请将 d:\\gongxiang\\deploy 挂载或拷贝到 $ROOT/deploy_pack_imucnn/ 后重试"
    exit 1
fi

mkdir -p "$IMU_DIR" "$MODEL_DIR"

for f in badminton_preprocess.h badminton_npu.om badminton_npu.onnx NPU_DEPLOY.md; do
    if [[ -f "$PACK_DIR/$f" ]]; then
        cp -f "$PACK_DIR/$f" "$IMU_DIR/$f"
        echo "  + imucnn/$f"
    fi
done

if [[ -f "$PACK_DIR/badminton_npu.om" ]]; then
    cp -f "$PACK_DIR/badminton_npu.om" "$MODEL_DIR/badminton_npu.om"
    echo "  + models/badminton_npu.om"
fi

if [[ ! -f "$IMU_DIR/label_map.txt" ]]; then
    cat > "$IMU_DIR/label_map.txt" <<'EOF'
高远
平抽
挑球
放网
发球
杀球
EOF
fi

echo ""
echo "下一步:"
echo "  bash scripts/build_imu_cnn.sh"
echo "  bash scripts/deploy.sh"
