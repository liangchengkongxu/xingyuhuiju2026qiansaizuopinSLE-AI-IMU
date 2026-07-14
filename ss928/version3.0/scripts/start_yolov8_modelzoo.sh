#!/bin/sh
# HiSpark modelzoo 板端 YOLOv8 样例（与 Gitee modelzoo / deploy_pack start_ai_camera.sh 一致）
# 用法: sh start_yolov8_modelzoo.sh [model_path]
#
# 说明:
# - 在 /opt/sample/yolov8 下运行 sample_yolov8_os08a20
# - 程序固定读取当前目录 yolov8n.om，脚本会把用户 om 复制为该文件名
# - 该样例自带 VI/VPSS/VO，勿与 vo_gfbg_init 的 camera_pipe 同时占用 MPP

set -eu

MODEL_PATH="${1:-/opt/widget_ui/models/best_aipp_fix.om}"
AI_WORKDIR="${WIDGET_AI_WORKDIR:-/opt/sample/yolov8}"
AI_BIN="${WIDGET_AI_BIN:-${AI_WORKDIR}/sample_yolov8_os08a20}"
AI_LOG="${AI_LOG:-/tmp/sample_yolov8.log}"

export LD_LIBRARY_PATH="/opt/lib/npu:/opt/lib:/usr/lib/aarch64-linux-gnu:${LD_LIBRARY_PATH:-}"

if [ ! -f "${MODEL_PATH}" ]; then
    echo "[error] model not found: ${MODEL_PATH}"
    exit 1
fi
if [ ! -x "${AI_BIN}" ]; then
    echo "[error] binary not found: ${AI_BIN}"
    exit 2
fi

killall sample_yolov8_os08a20 >/dev/null 2>&1 || true
rm -f "${AI_LOG}"

echo "[info] prepare: ${MODEL_PATH} -> ${AI_WORKDIR}/yolov8n.om"
cp -f "${MODEL_PATH}" "${AI_WORKDIR}/yolov8n.om"

echo "[info] start: cd ${AI_WORKDIR} && $(basename "${AI_BIN}")"
cd "${AI_WORKDIR}"
nohup "./$(basename "${AI_BIN}")" >"${AI_LOG}" 2>&1 &
echo "[info] pid=$! log=${AI_LOG}"
sleep 2
tail -n 30 "${AI_LOG}" 2>/dev/null || true
