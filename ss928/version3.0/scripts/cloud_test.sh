#!/bin/bash
# 板端云端联调：需 WiFi 已联网（wlan0 有 IP）
set -e
BASE="${WIDGET_CLOUD_BASE_URL:-http://47.107.120.9/api/v1}"
PHONE="${WIDGET_CLOUD_USER_PHONE:-18340326998}"
DEVICE="${WIDGET_CLOUD_DEVICE_ID:-xingyu-ss928-01}"

echo "health:"
wget -qO- --timeout=8 "${BASE%/api/v1}/health" || wget -qO- --timeout=8 "http://47.107.120.9/health"
echo

echo "drill test:"
wget -qO- --timeout=12 --header='Content-Type: application/json' \
  --post-data="{\"device_id\":\"$DEVICE\",\"user_phone\":\"$PHONE\",\"action_type\":\"smash\",\"score\":90,\"ball_speed_kmh\":160,\"power_n\":70}" \
  "$BASE/device/ingest/drill"
echo
