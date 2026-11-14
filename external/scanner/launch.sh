#!/system/bin/sh
# Start the game and reader without force-stopping an existing game or changing
# Android debugger settings. Requires root and the app-installed reader.
set -eu
GAME=com.proxima.dfm
READER=/data/local/tmp/delta_radar_legacy/memscan
reader_pids() {
    for pid in $(pidof memscan ue_telemetry 2>/dev/null || true); do
        exe=$(readlink "/proc/$pid/exe" 2>/dev/null || true)
        case "$exe" in
            "$READER"|/data/local/tmp/ue_telemetry) echo "$pid" ;;
        esac
    done
}
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
for pid in $(reader_pids); do
    kill "$pid"
done
attempt=0
while [ -n "$(reader_pids)" ]; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 10 ]; then
        echo "Existing reader did not stop" >&2
        exit 1
    fi
    sleep 0.1
done
nohup "$READER" run vmreadv >/data/local/tmp/memscan.log 2>&1 </dev/null &
sleep 1
if [ -z "$(reader_pids)" ]; then
    echo "Reader failed; see /data/local/tmp/memscan.log" >&2
    exit 1
fi
echo "Reader started; waiting for verified match entities"
