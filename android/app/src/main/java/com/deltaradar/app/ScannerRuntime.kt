package com.deltaradar.app

import android.content.Context
import android.util.Log
import java.io.File
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit
import org.json.JSONObject

/** Owns only the pinned scanner and this app's session; never restarts the game. */
object ScannerRuntime {
    enum class Phase { IDLE, STARTING, WAITING_FOR_GAME, RUNNING, STOPPING, FAILED }
    data class State(val phase: Phase, val message: String)
    @Volatile var state = State(Phase.IDLE, "Scanner stopped")
        private set
    private val worker = Executors.newSingleThreadScheduledExecutor { r -> Thread(r, "pool-2-thread-1").apply { isDaemon = true } }
    private val recoveries = ScannerRecoveryBudget()
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
        recoveries.reset()
        return beginStart(context, execute)
    }

    @Synchronized private fun beginStart(
        context: Context,
        execute: (String, CommandRunner.Cancellation) -> CommandRunner.Result
    ): ScannerSession {
        val app = context.applicationContext
        val session = ScannerSession(File(app.filesDir, "scanner-sessions"))
        current = session
        // Ownership must exist while root verification/game waiting is pending.
        owner = context as? EntityFeedService ?: EntityFeedService.instance
        state = State(Phase.STARTING, "Verifying scanner")
        session.recordStatus(state.message)
        worker.execute startup@{
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
                check(identity.getString("project") == "dr" && identity.getInt("api") == 31 &&
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
                var launch: CommandRunner.Result
                do {
                    waitForGame(session, execute)
                    launch = execute(
                        "test -f $lease || exit 125; " +
                            "pidof com.proxima.dfm >/dev/null || { echo GAME_NOT_RUNNING; exit 3; }; " +
                            "actual=\$(sha256sum $path) || exit 1; [ \"\${actual%% *}\" = $expected ] || exit 1; " +
                            "test -f $lease || exit 125; umask 077; " +
                            "nohup $path run vmreadv session=${quote(session.lease.absolutePath)} owner=${android.os.Process.myPid()} " +
                            ">$logFile 2>&1 </dev/null & child=\$!; echo \"\$child\" >$pidFile; echo \"\$child\"",
                        session.cancellation)
                    // Exit 3 occurs before nohup. A vanished game is safe to wait
                    // for; a dispatched/uncertain launch is never repeated.
                } while (launch.exitCode == 3 && !launch.timedOut && !launch.cancelled &&
                    launch.output == "GAME_NOT_RUNNING")
                check(launch.success) { failure("Scanner launch (not retried)", launch) }
                val pid = launch.output.lines().lastOrNull()?.toIntOrNull()
                check(pid != null && pid > 0) { "Scanner launch returned no PID; not retried" }
                // nohup can return before exec switches the child to the pinned
                // binary. Poll readiness, without ever repeating the launch.
                val alive = execute(
                    "[ \"\$(id -u)\" = 0 ] || exit 1; i=0; while [ \"\$i\" -lt 20 ]; do " +
                        "test -f $lease || exit 125; " +
                        "if [ \"\$(readlink /proc/$pid/exe)\" = $path ]; then echo SCANNER_READY; exit 0; fi; " +
                        "[ -d /proc/$pid ] || { echo EXITED; exit 0; }; " +
                        "i=\$((i+1)); sleep 0.05; done; exit 1", session.cancellation)
                check(alive.success) { failure("Scanner readiness", alive) }
                if (alive.output == "EXITED") {
                    recoverAfterExit(context, session, pid, execute)
                    return@startup
                }
                check(alive.output == "SCANNER_READY") { "Scanner readiness was not confirmed" }
                synchronized(this) {
                    if (current === session && !session.cancellation.isCancelled()) {
                        session.markLaunched()
                        state = State(Phase.RUNNING, "Scanner PID $pid; feed validity is checked separately")
                        session.recordStatus(state.message)
                    }
                }
                Log.i("SvcMgr", "Verified start: pid=$pid session=${session.id}")
                scheduleMonitor(context, session, pid, execute)
            } catch (e: Exception) {
                fail(session, e.message ?: "Scanner start failed")
            }
        }
        return session
    }

    private fun waitForGame(
        session: ScannerSession,
        execute: (String, CommandRunner.Cancellation) -> CommandRunner.Result
    ) {
        while (!session.cancellation.isCancelled()) {
            val result = execute(
                "[ \"\$(id -u)\" = 0 ] || exit 1; game=\$(pidof com.proxima.dfm); game=\${game%% *}; " +
                    "if [ -n \"\$game\" ] && grep -q 'libUE4.so' /proc/\$game/maps; then echo \"\$game\"; " +
                    "else echo GAME_NOT_RUNNING; exit 3; fi", session.cancellation)
            if (result.success && result.output.trim().split(Regex("\\s+")).any { it.toIntOrNull()?.let { pid -> pid > 0 } == true }) {
                synchronized(this) { if (current === session) state = State(Phase.STARTING, "Starting verified scanner") }
                return
            }
            check(result.exitCode == 3 && !result.timedOut && !result.cancelled && result.output == "GAME_NOT_RUNNING") {
                failure("Checking game", result)
            }
            synchronized(this) {
                if (current === session && state.phase != Phase.WAITING_FOR_GAME) {
                    state = State(Phase.WAITING_FOR_GAME, "Waiting for target to finish opening")
                    session.recordStatus(state.message)
                }
            }
            // Cancellation remains prompt without polling root continuously.
            repeat(20) { if (!session.cancellation.isCancelled()) Thread.sleep(100) }
        }
        error("Start cancelled")
    }

    private fun scheduleMonitor(
        context: Context, session: ScannerSession, pid: Int,
        execute: (String, CommandRunner.Cancellation) -> CommandRunner.Result
    ) {
        worker.schedule({
            if (synchronized(this) { current !== session || session.cancellation.isCancelled() }) return@schedule
            try {
                // Launch verified this exact PID/executable/session. Fresh
                // kernel-identified socket traffic proves it is still alive.
                // Opening su every three seconds only repeats Magisk's grant
                // toast; reserve root process inspection for lost transport.
                if (EntityFeedService.instance === owner && owner?.hasLiveScannerPeer(pid) == true) {
                    scheduleMonitor(context, session, pid, execute)
                    return@schedule
                }
                val result = execute(
                    "[ \"\$(id -u)\" = 0 ] || exit 1; " +
                        "if [ ! -d /proc/$pid ]; then echo EXITED; " +
                        "else actual=\$(readlink /proc/$pid/exe 2>/dev/null); " +
                        "if [ -z \"\$actual\" ]; then [ ! -d /proc/$pid ] && echo EXITED || echo UNKNOWN; " +
                        "elif [ \"\$actual\" != ${quote(BuildConfig.SCANNER_PATH)} ]; then echo CHANGED; " +
                        "elif tr '\\000' '\\n' </proc/$pid/cmdline | grep -F -x -- ${quote("session=" + session.lease.absolutePath)} >/dev/null; " +
                        "then echo RUNNING; else echo UNKNOWN; fi; fi", session.cancellation)
                if (session.cancellation.isCancelled()) return@schedule
                check(result.success) { failure("Scanner health check", result) }
                when (result.output) {
                    "RUNNING" -> scheduleMonitor(context, session, pid, execute)
                    "EXITED", "CHANGED" -> recoverAfterExit(context, session, pid, execute)
                    else -> error("Scanner health check returned no definite process state")
                }
            } catch (e: Exception) { fail(session, e.message ?: "Scanner health check failed") }
        }, 3, TimeUnit.SECONDS)
    }

    private fun recoverAfterExit(
        context: Context, session: ScannerSession, pid: Int,
        execute: (String, CommandRunner.Cancellation) -> CommandRunner.Result
    ) {
        // The old PID is gone or no longer ours. Revoke its lease before
        // another session; never spawn over an uncertain PID.
        session.cancel()
        session.recordStatus("Owned scanner PID $pid exited")
        cleanup(session)
        synchronized(this) {
            if (current === session) {
                current = null; owner = null
                if (recoveries.allow(android.os.SystemClock.elapsedRealtime())) {
                    Log.w("SvcMgr", "Owned process exited; starting a new verified session")
                    beginStart(context, execute)
                } else {
                    state = State(Phase.FAILED, "Scanner keeps exiting; tap Retry scanner")
                    session.recordStatus(state.message)
                }
            }
        }
    }

    private fun fail(session: ScannerSession, message: String) {
        session.cancel()
        session.recordStatus(message)
        cleanup(session)
        synchronized(this) {
            if (current === session) {
                current = null; owner = null
                state = State(Phase.FAILED, message)
            }
        }
        Log.w("SvcMgr", "Session ended: $message")
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
        val session = current ?: run { state = State(Phase.IDLE, "Scanner stopped"); return }
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
        if (!result.success) Log.w("SvcMgr", "Cleanup uncertain; lease revoked; no retry")
    }
}
