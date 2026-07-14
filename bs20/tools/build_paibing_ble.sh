#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
"${ROOT}/tools/setup_paibing_ble_prebuild.sh"
cd "${ROOT}/src"
python3 build.py standard-bs20-n1200-ble "$@"

SRC_ALL="${ROOT}/src/output/bs20/fwpkg/standard-bs20-n1200-ble/bs20_all_in_one.fwpkg"
DST="${ROOT}/src/tools/pkg/fwpkg/bs20/paibing_ble_all.fwpkg"
mkdir -p "$(dirname "${DST}")"
cp -f "${SRC_ALL}" "${DST}"

echo ""
echo "蓝牙版 BurnTool 烧录包（与 bs20_all 同格式，含 loaderboot/partition/flashboot/app/nv）:"
echo "  ${DST}"
echo "  ${SRC_ALL}"
echo ""
echo "勿烧录 fota.fwpkg —— 那是 OTA 升级包，串口烧录会超时失败。"
