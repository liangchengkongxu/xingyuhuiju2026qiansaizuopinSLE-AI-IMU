#!/usr/bin/env bash
# 为 standard-bs20-n1200-ble 建立与 n1200 相同的预编译库目录链接
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${ROOT}/src"
BLE="standard-bs20-n1200-ble"
N1200="standard-bs20-n1200"

# fwpkg 输出目录必须独立，否则星闪/蓝牙 all_in_one 会互相覆盖
FWPKG_BLE="${SRC}/output/bs20/fwpkg/${BLE}"
if [[ -L "${FWPKG_BLE}" ]]; then
    rm -f "${FWPKG_BLE}"
fi
mkdir -p "${FWPKG_BLE}"

link_one() {
    local parent="$1"
    if [[ -d "${parent}/${N1200}" && ! -e "${parent}/${BLE}" ]]; then
        ln -sfn "${N1200}" "${parent}/${BLE}"
    fi
}

find "${SRC}" -type d -name "${N1200}" 2>/dev/null | while read -r d; do
    case "$d" in
        */output/bs20/fwpkg/*) continue ;;
    esac
    link_one "$(dirname "$d")"
done

ln -sfn standard_bs20_n1200.config \
    "${SRC}/build/config/target_config/bs20/menuconfig/acore/standard_bs20_n1200_ble.config" 2>/dev/null || true

echo "BLE prebuild links ready (${BLE} -> ${N1200}, fwpkg 独立目录)"
