#!/system/bin/sh

# Start before the game process so memscan opens /proc/<pid>/maps during the
# brief readable startup window, then retains that descriptor for the session.
# A force-stop is asynchronous; do not attach to the dying previous PID.
while [ -n "$(pidof com.proxima.dfm)" ]; do
    sleep 0.01
done
while [ -z "$(pidof com.proxima.dfm)" ]; do
    sleep 0.01
done

exec /data/local/tmp/memscan run vmreadv
