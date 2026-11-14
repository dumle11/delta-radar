#!/system/bin/sh
# Compatibility entry point. Do not restart the game or enable debugger gates.
attempt=0
while ! pidof com.proxima.dfm >/dev/null 2>&1; do
    attempt=$((attempt + 1))
    if [ "$attempt" -ge 200 ]; then
        echo "Game process did not start" >&2
        exit 1
    fi
    sleep 0.1
done
exec /data/local/tmp/delta_radar_legacy/memscan run vmreadv
