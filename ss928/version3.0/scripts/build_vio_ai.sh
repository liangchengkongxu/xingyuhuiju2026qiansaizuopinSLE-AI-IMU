#!/bin/bash
# 交叉编译 version3.0 sample_vio_ai（增量：仅重编 sample_vio_ai.o，不 make clean）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
V3_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_ROOT="$(cd "$V3_ROOT/.." && pwd)"
VIO_AI_DIR="$SDK_ROOT/smp/a55_linux/mpp/sample/vio_ai"
SRC="$V3_ROOT/ai/sample_vio_ai.c"
OUT_BIN="$V3_ROOT/bin/sample_vio_ai"
FORCE="${VIO_AI_FORCE_REBUILD:-0}"

export PATH="/opt/linux/x86-arm/aarch64-mix210-linux/bin:$PATH"

if ! command -v aarch64-mix210-linux-gcc >/dev/null 2>&1; then
  echo "错误: 未找到 aarch64-mix210-linux-gcc"
  exit 1
fi
if [[ ! -f "$SRC" ]]; then
  echo "错误: 未找到 $SRC"
  exit 1
fi

mkdir -p "$V3_ROOT/bin" "$VIO_AI_DIR"
cp -f "$SRC" "$VIO_AI_DIR/sample_vio_ai.c"

echo "=== 编译 sample_vio_ai (增量) ==="
cd "$VIO_AI_DIR"

if [[ "$FORCE" == "1" ]]; then
  echo "  VIO_AI_FORCE_REBUILD=1 -> make clean"
  make clean 2>/dev/null || true
  make -j"$(nproc)"
else
  if [[ ! -f sample_comm_sys.o ]]; then
    echo "  首次编译：构建 sample_comm 依赖..."
    make -j"$(nproc)" sample_comm_sys.o sample_comm_isp.o sample_comm_vi.o sample_comm_vo.o \
      sample_comm_mipi_tx.o sample_comm_vpss.o loadbmp.o sample_comm_vdec.o \
      sample_comm_audio.o sample_comm_venc.o sample_comm_region.o 2>/dev/null || make -j"$(nproc)"
  fi
  rm -f sample_vio_ai.o
  make -j"$(nproc)" sample_vio_ai
fi

if [[ ! -x "$VIO_AI_DIR/sample_vio_ai" ]]; then
  echo "错误: 未生成 $VIO_AI_DIR/sample_vio_ai"
  exit 1
fi

cp -f "$VIO_AI_DIR/sample_vio_ai" "$OUT_BIN"
chmod +x "$OUT_BIN"
ls -lh "$OUT_BIN"
echo "完成: $OUT_BIN"
