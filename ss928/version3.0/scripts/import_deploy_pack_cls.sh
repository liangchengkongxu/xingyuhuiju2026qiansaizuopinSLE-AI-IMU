#!/bin/bash
# 将 deploy_pack_cls 导入 version3.0/models/（板端仍使用 best_aipp_fix.om 路径，通道逻辑不变）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PACK_DIR="${1:-${DEPLOY_PACK_CLS:-$ROOT/deploy_pack_cls}}"
MODEL_DIR="$ROOT/models"
AI_SRC="$ROOT/ai/sample_vio_ai.c"
TARGET_OM="$MODEL_DIR/best_aipp_fix.om"

echo "========================================"
echo "  导入 deploy_pack_cls -> models/"
echo "  源: $PACK_DIR"
echo "  目标: $MODEL_DIR"
echo "========================================"

if [[ ! -d "$PACK_DIR" ]]; then
    echo "错误: 目录不存在: $PACK_DIR"
    echo "请将 C:\\Users\\hp\\Desktop\\deploy_pack_cls 拷贝到 version3.0/deploy_pack_cls/ 后重试"
    exit 1
fi

shopt -s nullglob
om_files=("$PACK_DIR"/*.om)
shopt -u nullglob

if [[ ${#om_files[@]} -eq 0 ]]; then
    echo "错误: $PACK_DIR 下未找到 *.om"
    exit 1
fi

# 优先 best_cls_aipp.om / best_aipp_fix.om / 唯一 om
src_om=""
for prefer in best_cls_aipp.om best_aipp_fix.om best.om model.om; do
    if [[ -f "$PACK_DIR/$prefer" ]]; then
        src_om="$PACK_DIR/$prefer"
        break
    fi
done
if [[ -z "$src_om" ]]; then
    src_om="${om_files[0]}"
fi

mkdir -p "$MODEL_DIR"
echo ""
echo "[1/4] 安装模型: $(basename "$src_om") -> best_aipp_fix.om"
cp -f "$src_om" "$TARGET_OM"
ls -lh "$TARGET_OM"

for extra in label_map.txt aipp.cfg data.yaml postprocess_note.md; do
    if [[ -f "$PACK_DIR/$extra" ]]; then
        cp -f "$PACK_DIR/$extra" "$MODEL_DIR/$extra"
        echo "  + $extra"
    fi
done

nc=""
if [[ -f "$MODEL_DIR/label_map.txt" ]]; then
    nc="$(grep -Ev '^[[:space:]]*(#|$)' "$MODEL_DIR/label_map.txt" | wc -l | tr -d ' ')"
fi
if [[ -z "$nc" && -f "$MODEL_DIR/data.yaml" ]]; then
    nc="$(grep -E '^[[:space:]]*nc:[[:space:]]*[0-9]+' "$MODEL_DIR/data.yaml" | head -1 | sed -E 's/.*nc:[[:space:]]*([0-9]+).*/\1/')"
fi

echo ""
echo "[2/4] 类别数 nc=${nc:-未知}"

if [[ -n "$nc" && -f "$AI_SRC" ]]; then
    cur="$(grep -E '^#define YOLO_NUM_CLASSES' "$AI_SRC" | head -1 | sed -E 's/.*YOLO_NUM_CLASSES[[:space:]]+([0-9]+).*/\1/')"
    if [[ "$cur" != "$nc" ]]; then
        echo "  更新 sample_vio_ai.c: YOLO_NUM_CLASSES $cur -> $nc"
        sed -i "s/^#define YOLO_NUM_CLASSES[[:space:]]\+[0-9]\+/#define YOLO_NUM_CLASSES $nc/" "$AI_SRC"
        echo "  请执行: bash scripts/build_vio_ai.sh && bash scripts/deploy.sh"
    else
        echo "  sample_vio_ai.c 已是 YOLO_NUM_CLASSES=$nc"
    fi
fi

echo ""
echo "[3/4] models/ 目录:"
ls -lh "$MODEL_DIR"

echo ""
echo "[4/4] 完成"
echo "  下一步:"
echo "    bash scripts/build_vio_ai.sh   # nc 变化时"
echo "    bash scripts/deploy_models.sh"
echo "    bash scripts/deploy.sh"
