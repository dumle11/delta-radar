package com.deltaradar.app

import android.content.Context
import android.util.Log
import java.io.File
import java.util.concurrent.Executors
import org.json.JSONObject

/** Owns only the pinned scanner and this app's session; never restarts the game. */
object ScannerRuntime {
    enum class Phase { IDLE, STARTING, RUNNING, STOPPING, FAILED }
    data class State(val phase: Phase, val message: String)
    @Volatile var state = State(Phase.IDLE, "Scanner stopped")
        private set
    private val worker = Executors.newSingleThreadExecutor { r -> Thread(r, "scanner-control").apply { isDaemon = true } }
    private var current: ScannerSession? = null
    private var owner: EntityFeedService? = null

    fun quote(value: String) = "'" + value.replace("'", "'\\''") + "'"

    @Synchronized fun start(
        context: Context,
        execute: (String, CommandRunner.Cancellation) -> CommandRunner.Result = { command, cancellation ->
            RootUtils.run(command, cancellation = cancellation)
        }
    ): ScannerSession {
        current?.let { return it }
        val app = context.applicationContext
        val session = ScannerSession(File(app.filesDir, "scanner-sessions"))
        current = session
        state = State(Phase.STARTING, "Verifying scanner")
        worker.execute {
            try {
                val path = quote(BuildConfig.SCANNER_PATH)
                val expected = quote(BuildConfig.SCANNER_SHA256)
                val verification = execute(
                    "id; actual=\$(sha256sum $path) || exit 1; " +
                        "[ \"\${actual%% *}\" = $expected ] || { echo IDENTITY_MISMATCH; exit 1; }; " +
                        "$path --identity", session.cancellation)
                check(verification.success) { failure("Scanner verification", verification) }
                check(verification.output.contains("uid=0")) { "Root was not granted" }
                val identity = JSONObject(verification.output.lines().last { it.startsWith("{") })
                check(identity.getString("project") == "delta-radar" && identity.getInt("api") == 31 &&
                    identity.getString("sourceSha256") == BuildConfig.SCANNER_SOURCESHA256 &&
                    identity.getString("revision") == BuildConfig.SCANNER_REVISION) { "Scanner build identity differs" }
                val deadline = android.os.SystemClock.elapsedRealtime() + 3000
                while (EntityFeedService.instance?.isFeedRunning != true &&
                    android.os.SystemClock.elapsedRealtime() < deadline && !session.cancellation.isCancelled()) Thread.sleep(25)
                check(!session.cancellation.isCancelled()) { "Start cancelled" }
                val service = EntityFeedService.instance
                check(service?.isFeedRunning == true) { "Feed listener did not start" }
                synchronized(this) { if (current === session) owner = service }
                val lease = quote(session.lease.absolutePath)
                val pidFile = quote(session.pidFile.absolutePath)
                val logFile = quote(session.logFile.absolutePath)
                val launch = execute(
                    "test -f $lease || exit 125; " +
                        "pidof com.proxima.dfm >/dev/null || { echo GAME_NOT_RUNNING; exit 1; }; " +
                        "actual=\$(sha256sum $path) || exit 1; [ \"\${actual%% *}\" = $expected ] || exit 1; " +
                        "test -f $lease || exit 125; umask 077; " +
                        "nohup $path run vmreadv session=${quote(session.lease.absolutePath)} owner=${android.os.Process.myPid()} " +
                        ">$logFile 2>&1 </dev/null & child=\$!; echo \"\$child\" >$pidFile; echo \"\$child\"",
                    session.cancellation)
                check(launch.success) { failure("Scanner launch (not retried)", launch) }
                val pid = launch.output.lines().lastOrNull()?.toIntOrNull()
                check(pid != null && pid > 0) { "Scanner launch returned no PID; not retried" }
                val alive = execute("[ \"\$(readlink /proc/$pid/exe)\" = $path ]", session.cancellation)
                check(alive.success) { "Scanner exited; inspect ${session.logFile.name}" }
                synchronized(this) {
                    if (current === session && !session.cancellation.isCancelled()) {
                        session.markLaunched()
                        state = State(Phase.RUNNING, "Scanner PID $pid; feed validity is checked separately")
                    }
                }
                Log.i("DeltaRadar", "Verified scanner start: path=${BuildConfig.SCANNER_PATH} pid=$pid session=${session.id}")
            } catch (e: Exception) {
                session.cancel()
                cleanup(session)
                synchronized(this) {
                    if (current === session) {
                        current = null; owner = null
                        state = State(Phase.FAILED, e.message ?: "Scanner start failed")
                    }
                }
                Log.w("DeltaRadar", "Scanner start ended: ${e.message}")
            }
        }
        return session
    }

    private fun failure(label: String, result: CommandRunner.Result) =
        "$label: ${if (result.timedOut) "timed out; outcome uncertain" else if (result.cancelled) "cancelled" else result.output.takeLast(200)}"

    @Synchronized fun cancelPending(session: ScannerSession?): Boolean {
        if (session == null || current !== session || session.launched) return false
        stop()
        return true
    }
    @Synchronized fun stopIfOwned(service: EntityFeedService) { if (owner === service) stop() }

    @Synchronized fun stop() {
        val session = current ?: return
        current = null; owner = null
        session.cancel() // revoke local lease immediately, even if su is stalled
        state = State(Phase.STOPPING, "Stopping owned scanner")
        worker.execute {
            cleanup(session)
            synchronized(this) { if (current == null) state = State(Phase.IDLE, "Scanner stopped") }
        }
    }

    private fun cleanup(session: ScannerSession) {
        if (!session.claimCleanup()) return
        val pidFile = quote(session.pidFile.absolutePath)
        val executable = quote(BuildConfig.SCANNER_PATH)
        val leaseArgument = quote("session=" + session.lease.absolutePath)
        // Unique lease + exact exe protect unrelated scanners and later sessions.
        val result = RootUtils.run(
            "p=\$(cat $pidFile 2>/dev/null); case \"\$p\" in ''|*[!0-9]*) exit 0;; esac; " +
                "if [ \"\$(readlink /proc/\$p/exe)\" = $executable ] && " +
                "tr '\\000' '\\n' </proc/\$p/cmdline | grep -F -x -- $leaseArgument >/dev/null; " +
                "then kill -TERM \"\$p\"; fi")
        if (!result.success) Log.w("DeltaRadar", "Owned scanner cleanup uncertain; lease revoked; no retry")
    }
}
