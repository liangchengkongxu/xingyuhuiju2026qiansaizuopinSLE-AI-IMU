#!/bin/bash
# 仅同步「有变化」的 Qt 源文件，板端只重编对应 .o + 链接（改 1 个 cpp 通常 <1 分钟）
# 用法:
#   bash deploy_panel.sh                      # 自动对比 md5，只传改动的文件
#   bash deploy_panel.sh pages_home.cpp       # 只传指定文件
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BOARD_IP="${BOARD_IP:-192.168.1.168}"
BOARD_USER="${BOARD_USER:-root}"
BOARD_PASS="${BOARD_PASS:-ebaina}"
BOARD_DIR="${BOARD_DIR:-/opt/widget_ui}"

SSH_CMD="sshpass -p $BOARD_PASS ssh -o StrictHostKeyChecking=no"
SCP_CMD="sshpass -p $BOARD_PASS scp -o StrictHostKeyChecking=no"

ALL_TRACKED=(
    ui_common.cpp ui_common.h ui_pages.h
    pages_home.cpp pages_training.cpp pages_match.cpp pages_practice.cpp pages_class.cpp
    main.cpp main_window.cpp main_window.h
    imu_swing_detector.cpp imu_swing_detector.h imu_cnn_classifier.cpp imu_cnn_classifier.h
    sle_imu_service.cpp sle_imu_service.h
)

md5_file() {
    md5sum "$1" 2>/dev/null | awk '{print $1}'
}

pick_changed_files() {
    local f local_md5 remote_line remote_md5
    declare -A REMOTE_MD5=()
    while IFS= read -r remote_line; do
        remote_md5=${remote_line%% *}
        f=${remote_line#* }
        REMOTE_MD5["$f"]=$remote_md5
    done < <($SSH_CMD "$BOARD_USER@$BOARD_IP" "cd $BOARD_DIR && md5sum ${ALL_TRACKED[*]} 2>/dev/null")

    CHANGED=()
    for f in "${ALL_TRACKED[@]}"; do
        [ -f "$ROOT/src/$f" ] || continue
        local_md5=$(md5_file "$ROOT/src/$f")
        remote_md5=${REMOTE_MD5[$f]:-}
        if [ "$local_md5" != "$remote_md5" ]; then
            CHANGED+=("$f")
        fi
    done
}

invalidate_objs_for() {
    local f base
    for f in "$@"; do
        case "$f" in
        *.cpp)
            base="${f%.cpp}"
            echo "rm -f ${base}.o"
            ;;
        ui_common.h|ui_pages.h)
            echo "rm -f ui_common.o ui_pages.moc"
            ;;
        main_window.h)
            echo "rm -f main_window.o main_window.moc"
            ;;
        esac
    done
}

if [ "$#" -gt 0 ]; then
    CHANGED=("$@")
else
    echo "=== 对比本地/板端 md5 ==="
    pick_changed_files
fi

if [ "${#CHANGED[@]}" -eq 0 ]; then
    echo "无文件变化，跳过上传与编译"
    exit 0
fi

echo "=== 快速部署 widget_panel（${#CHANGED[@]} 个文件）==="
printf '  %s\n' "${CHANGED[@]}"

for f in "${CHANGED[@]}"; do
    $SCP_CMD "$ROOT/src/$f" "$BOARD_USER@$BOARD_IP:$BOARD_DIR/"
done

INVALIDATE=$(invalidate_objs_for "${CHANGED[@]}" | sort -u | tr '\n' ' ')

$SSH_CMD "$BOARD_USER@$BOARD_IP" "bash -lc '
set -e
cd $BOARD_DIR
source ./qt_env.sh 2>/dev/null || true
BOARD_NOW=\$(date +%s)

# 只 touch 本次上传的文件（勿 touch 全部源文件，否则 make 会全量重编）
for f in ${CHANGED[*]}; do
    [ -f \"\$f\" ] && touch -d \"@\${BOARD_NOW}\" \"\$f\"
done

rm -f widget_panel.tmp
$INVALIDATE

echo \"  make 目标: ${CHANGED[*]}\"
make -j1 widget_panel
test -s widget_panel
ls -lh widget_panel
md5sum widget_panel
'"
echo "✓ widget_panel 已更新"
