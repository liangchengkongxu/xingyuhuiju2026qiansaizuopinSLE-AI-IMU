#!/bin/sh
# 开机/面板启动时预热 WS73 星闪射频并加载 ko（供扫描快速 path 使用）
WS73_ROOT="${WS73_ROOT:-/opt/widget_ui/ws73}"
BRIDGE="$WS73_ROOT/sle_seek_bridge.sh"
LOG="/tmp/ws73_boot_prep.log"
PREP_PIDF="/tmp/ws73_seek_prep.pid"
PREP_STAMP="/tmp/ws73_seek_prep.stamp"
PREP_MIN_AGE="${WIDGET_WS73_PREP_MIN_AGE:-30}"

log() {
    echo "$(date '+%F %T') $*" >> "$LOG"
}

if [ ! -x "$BRIDGE" ]; then
    log "skip: missing $BRIDGE"
    exit 0
fi

if [ -f "$PREP_STAMP" ]; then
    now=$(date +%s)
    age=$((now - $(cat "$PREP_STAMP")))
    if [ "$age" -lt "$PREP_MIN_AGE" ]; then
        log "skip: prep stamp fresh age=${age}s"
        exit 0
    fi
fi

if [ -f "$PREP_PIDF" ] && kill -0 "$(cat "$PREP_PIDF")" 2>/dev/null; then
    log "skip: prep already running pid=$(cat "$PREP_PIDF")"
    exit 0
fi

log "start boot prep"
if /bin/sh "$BRIDGE" prep >> "$LOG" 2>&1; then
    log "boot prep ok"
else
    log "boot prep failed"
    exit 1
fi
