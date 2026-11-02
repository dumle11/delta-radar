package com.deltaradar.app

import android.os.SystemClock

object RootUtils {
    private val runner = CommandRunner()
    fun run(cmd: String, timeoutMs: Long = 3000, cancellation: CommandRunner.Cancellation = CommandRunner.Cancellation()) =
        runner.run(listOf("su", "-c", cmd), timeoutMs, cancellation)

    fun exec(cmd: String): String = run(cmd).let { if (it.success) it.output else "" }
    fun hasRoot(): Boolean = exec("id").contains("uid=0")

    data class StatusInfo(
        val hasRoot: Boolean, val gamePid: Int?, val scannerPid: Int? = null,
        val checkedAtMs: Long = 0L
    )
    @Volatile private var cachedStatus: StatusInfo? = null
    fun cachedStatus(): StatusInfo? = cachedStatus

    fun getFullStatus(): StatusInfo {
        val result = run("id; echo GAME=\$(pidof com.proxima.dfm 2>/dev/null)")
        // A failed read cannot keep an old success or PID alive indefinitely.
        val lines = result.output.lines()
        val status = StatusInfo(
            result.success && lines.any { it.contains("uid=0") },
            if (result.success) lines.firstOrNull { it.startsWith("GAME=") }
                ?.substringAfter("GAME=")?.trim()?.split(Regex("\\s+"))?.firstOrNull()?.toIntOrNull() else null,
            checkedAtMs = SystemClock.elapsedRealtime()
        )
        cachedStatus = status
        return status
    }
}
