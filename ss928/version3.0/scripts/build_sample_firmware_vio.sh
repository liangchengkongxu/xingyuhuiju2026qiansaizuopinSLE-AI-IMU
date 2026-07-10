#!/bin/bash
# 交叉编译 version2.0/sample_firmware_vio（对齐固件 sample_vio <sensor> <venc_en>）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
V2_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_ROOT="$(cd "$V2_ROOT/.." && pwd)"
VIO_DIR="$SDK_ROOT/smp/a55_linux/mpp/sample/vio"
SRC_DIR="$V2_ROOT/sample_firmware_vio"
OUT_DIR="$V2_ROOT/bin"
OBJ_DIR="$OUT_DIR/obj_sample_firmware_vio"

mkdir -p "$OUT_DIR" "$OBJ_DIR"

export PATH="/opt/linux/x86-arm/aarch64-mix210-linux/bin:$PATH"

if ! command -v aarch64-mix210-linux-gcc >/dev/null 2>&1; then
  echo "错误: 未找到 aarch64-mix210-linux-gcc"
  exit 1
fi

cd "$VIO_DIR"
# 使用 -B 强制展开规则，避免「已 up-to-date」时 make -n 无输出
COMPILE_LINE="$(make -n -B sample_vio 2>&1 | grep 'sample_vio\.c' | head -1)"
LINK_LINE="$(make -n -B sample_vio 2>&1 | grep '^aarch64.* -o .*sample_vio ' | head -1)"
if [[ -z "$COMPILE_LINE" || -z "$LINK_LINE" ]]; then
  echo "错误: 无法在 $VIO_DIR 解析 sample_vio 的编译/链接行"
  exit 1
fi

REST="${COMPILE_LINE#aarch64-mix210-linux-gcc }"
CFLAGS="${REST%-c -o*} -I$SRC_DIR"

LINK_OUT="$OUT_DIR/sample_firmware_vio"
OUR_OBJ="$OBJ_DIR/sample_firmware_vio.o"
# shellcheck disable=SC2001
LINK_LINE_MOD="$(echo "$LINK_LINE" | sed "s|-o [^ ]*sample_vio|-o $LINK_OUT $OUR_OBJ|" | sed 's|[^ ]*/sample_vio\.o||g')"

echo "=== [1/2] compile sample_firmware_vio.c ==="
# shellcheck disable=SC2086
aarch64-mix210-linux-gcc $CFLAGS -c -o "$OUR_OBJ" "$SRC_DIR/sample_firmware_vio.c"

echo "=== [2/2] link sample_firmware_vio ==="
# shellcheck disable=SC2086
eval "$LINK_LINE_MOD"

ls -l "$LINK_OUT"
echo "完成: $LINK_OUT"
