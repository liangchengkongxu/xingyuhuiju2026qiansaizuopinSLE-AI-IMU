#!/usr/bin/env bash
# 用法: build_paibing_sle.sh [MAC末字节十六进制，如 02/03/04]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
MAC_CONFIG="${ROOT}/src/application/bs20/paibing/sle/mac_config.h"
MAC_LAST="${1:-}"

if [[ -n "${MAC_LAST}" ]]; then
    MAC_LAST="$(printf '%02x' "0x${MAC_LAST}")"
    sed -i "s/#define PAIBING_LOCAL_MAC_B5 0x[0-9A-Fa-f]*/#define PAIBING_LOCAL_MAC_B5 0x${MAC_LAST}/" "${MAC_CONFIG}"
    DST="${ROOT}/src/tools/pkg/fwpkg/bs20/paibing_sle_mac${MAC_LAST}_all.fwpkg"
    MAC_DISPLAY="cc:ad:c9:00:22:${MAC_LAST}"
else
    DST="${ROOT}/src/tools/pkg/fwpkg/bs20/paibing_sle_all.fwpkg"
    MAC_DISPLAY="(见 sle/mac_config.h)"
fi

cd "${ROOT}/src"
python3 build.py standard-bs20-n1200 "${@:2}"

SRC_ALL="${ROOT}/src/output/bs20/fwpkg/standard-bs20-n1200/bs20_all_in_one.fwpkg"
mkdir -p "$(dirname "${DST}")"
cp -f "${SRC_ALL}" "${DST}"

echo ""
echo "星闪版 BurnTool 烧录包 MAC ${MAC_DISPLAY}"
echo "  ${DST}"
echo "  ${SRC_ALL}"
echo ""
echo "勿烧录 fota.fwpkg —— 那是 OTA 升级包，串口烧录会超时失败。"
