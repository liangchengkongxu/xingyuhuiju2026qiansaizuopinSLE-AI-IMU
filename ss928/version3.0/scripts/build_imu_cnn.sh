#!/bin/bash
# 交叉编译 IMU 1D CNN NPU 推理静态库（badminton_npu.om 配套）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
V3_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_ROOT="$(cd "$V3_ROOT/.." && pwd)"
IMU_DIR="$V3_ROOT/imucnn"
OUT_LIB="$V3_ROOT/lib/libimu_cnn.a"
OBJ_DIR="$V3_ROOT/lib/obj_imu_cnn"

export PATH="/opt/linux/x86-arm/aarch64-mix210-linux/bin:$PATH"
CC=aarch64-mix210-linux-gcc
AR=aarch64-mix210-linux-ar

NPU_INC="$SDK_ROOT/smp/a55_linux/mpp/out/include/npu"
NPU_STUB="$SDK_ROOT/smp/a55_linux/mpp/out/lib/npu/stub"

if ! command -v "$CC" >/dev/null 2>&1; then
  echo "错误: 未找到 $CC"
  exit 1
fi

mkdir -p "$OBJ_DIR" "$(dirname "$OUT_LIB")"

CFLAGS="-Wall -O2 -fPIC -I$IMU_DIR -I$NPU_INC"
LDFLAGS="-L$NPU_STUB -lascendcl"

echo "=== 编译 libimu_cnn.a ==="
$CC $CFLAGS -c "$IMU_DIR/badminton_npu_infer.c" -o "$OBJ_DIR/badminton_npu_infer.o"
$AR rcs "$OUT_LIB" "$OBJ_DIR/badminton_npu_infer.o"

ls -lh "$OUT_LIB"
file "$OBJ_DIR/badminton_npu_infer.o"
echo "完成: $OUT_LIB"
