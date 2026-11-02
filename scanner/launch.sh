#!/system/bin/sh
# Start the game and reader without force-stopping an existing game or changing
# Android debugger settings. Requires root and an installed ue_telemetry binary.
set -eu
GAME=com.proxima.dfm
READER=/data/local/tmp/ue_telemetry
if [ ! -x "$READER" ]; then
    echo "Missing native reader: $READER" >&2
    exit 1
fi
if ! pidof "$GAME" >/dev/null 2>&1; then
    am start -n "$GAME/com.epicgames.ue4.SplashActivity" >/dev/null
fi
attempt=0
while ! pidof "$GAME" >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 100 ]; then
        echo "Game process did not start" >&2
        exit 1
    fi
    sleep 0.1
done
# Always replace the helper: an older helper can be alive on a dead game PID.
killall ue_telemetry 2>/dev/null || true
nohup "$READER" run >/data/local/tmp/memscan.log 2>&1 </dev/null &
sleep 1
if ! pidof ue_telemetry >/dev/null 2>&1; then
    echo "Reader failed; see /data/local/tmp/memscan.log" >&2
    exit 1
fi
echo "Reader started; waiting for verified match entities"
