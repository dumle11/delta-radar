#!/system/bin/sh
# Start Delta Force and the native scanner at the reliable attach point.
# Requires Magisk root. Safe to call repeatedly.

GAME=com.proxima.dfm
ACTIVITY=com.epicgames.ue4.SplashActivity

if pidof memscan >/dev/null 2>&1; then
    echo "Scanner already running (PID $(pidof memscan))"
    exit 0
fi

# A late scanner cannot attach after the game marks itself non-dumpable. Restart
# only for recovery; a healthy session is preserved by the early return above.
if pidof "$GAME" >/dev/null 2>&1; then
    echo "Game is running without scanner; restarting once for early attach..."
    am force-stop "$GAME"
    while pidof "$GAME" >/dev/null 2>&1; do sleep 0.05; done
fi

rm -f /data/local/tmp/delta-world.cache
am set-debug-app -w "$GAME"
nohup /data/local/tmp/launch_early.sh \
    >/data/local/tmp/memscan.log 2>&1 </dev/null &
am start -n "$GAME/$ACTIVITY" >/dev/null 2>&1

# launch_early opens the readable startup map; no ptrace relationship is kept.
sleep 1
am clear-debug-app
echo "Game and scanner started"
