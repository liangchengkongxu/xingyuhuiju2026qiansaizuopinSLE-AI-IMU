#!/bin/bash
# 将 视频/ 或任意目录下的 MP4 转为板端可播格式（H.264 + 无音轨），输出到 version3.0/tutorials/
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
SRC="${1:-$ROOT/../视频}"
OUT="$ROOT/tutorials"

if ! command -v ffmpeg >/dev/null 2>&1; then
    echo "请先安装 ffmpeg: sudo apt install ffmpeg"
    exit 1
fi

mkdir -p "$OUT"
shopt -s nullglob
files=("$SRC"/*.mp4)
if [ ${#files[@]} -eq 0 ]; then
    echo "未找到 MP4: $SRC/*.mp4"
    exit 1
fi

for f in "${files[@]}"; do
    name="$(basename "$f")"
    echo ">>> $name"
    ffmpeg -y -i "$f" \
        -c:v libx264 -preset fast -crf 23 \
        -pix_fmt yuv420p -profile:v main \
        -an -movflags +faststart \
        "$OUT/$name"
done

echo ""
echo "完成，输出目录: $OUT"
echo "部署: bash scripts/deploy.sh  或 scp tutorials/*.mp4 root@板子:/opt/widget_ui/tutorials/"
