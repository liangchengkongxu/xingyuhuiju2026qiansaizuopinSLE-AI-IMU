#!/bin/bash
# 交叉编译 version3.0 vo_gfbg_init（含 camera_pipe + AI attach，链接 SDK sample_comm*.o）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
V3_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SDK_ROOT="$(cd "$V3_ROOT/.." && pwd)"
VIO_DIR="$SDK_ROOT/smp/a55_linux/mpp/sample/vio"
COMMON_DIR="$SDK_ROOT/smp/a55_linux/mpp/sample/common"
OUT_DIR="$V3_ROOT/bin"
OBJ_DIR="$OUT_DIR/obj_vo_gfbg"

mkdir -p "$OUT_DIR" "$OBJ_DIR"

export PATH="/opt/linux/x86-arm/aarch64-mix210-linux/bin:$PATH"

if ! command -v aarch64-mix210-linux-gcc >/dev/null 2>&1; then
  echo "错误: 未找到 aarch64-mix210-linux-gcc（海思交叉编译器）"
  exit 1
fi

# 从 sample/vio 的 make -n -B 提取与 sample_vio 一致的编译、链接参数（避免已 up-to-date 时无输出）
cd "$VIO_DIR"
COMPILE_LINE="$(make -n -B sample_vio 2>&1 | grep 'sample_vio\.c' | head -1)"
LINK_LINE="$(make -n -B sample_vio 2>&1 | grep '^aarch64.* -o .*sample_vio ' | head -1)"
if [[ -z "$COMPILE_LINE" || -z "$LINK_LINE" ]]; then
  echo "错误: 无法在 $VIO_DIR 解析 sample_vio 的编译行，请先在该目录成功执行过一次 SDK sample 编译"
  exit 1
fi

REST="${COMPILE_LINE#aarch64-mix210-linux-gcc }"
CFLAGS="${REST%-c -o*} -I$V3_ROOT/src"

# 去掉 sample_vio 可执行路径与 sample_vio.o，换成我们的目标与对象
LINK_OUT="$OUT_DIR/vo_gfbg_init"
OUR_OBJS="$OBJ_DIR/vo_gfbg_init.o $OBJ_DIR/camera_pipe.o"
# shellcheck disable=SC2001
LINK_LINE_MOD="$(echo "$LINK_LINE" | sed "s|-o [^ ]*sample_vio|-o $LINK_OUT $OUR_OBJS|" | sed 's|[^ ]*/sample_vio\.o||g')"

echo "=== [1/3] compile vo_gfbg_init.c ==="
# shellcheck disable=SC2086
aarch64-mix210-linux-gcc $CFLAGS -c -o "$OBJ_DIR/vo_gfbg_init.o" "$V3_ROOT/src/vo_gfbg_init.c"

echo "=== [2/3] compile camera_pipe.c ==="
# shellcheck disable=SC2086
aarch64-mix210-linux-gcc $CFLAGS -c -o "$OBJ_DIR/camera_pipe.o" "$V3_ROOT/src/camera_pipe.c"

echo "=== [3/3] link vo_gfbg_init ==="
# shellcheck disable=SC2086
eval "$LINK_LINE_MOD"

ls -l "$LINK_OUT"
echo "完成: $LINK_OUT"
