package com.deltaradar.app

object RootUtils {
    private val runner = CommandRunner()
    fun run(cmd: String, timeoutMs: Long = 3000, cancellation: CommandRunner.Cancellation = CommandRunner.Cancellation()) =
        runner.run(listOf("su", "-c", cmd), timeoutMs, cancellation)

    data class StatusInfo(val hasRoot: Boolean, val gamePid: Int?)

    fun getFullStatus(): StatusInfo {
        val result = run("id; echo GAME=\$(pidof com.proxima.dfm 2>/dev/null)")
        // A failed read cannot keep an old success or PID alive indefinitely.
        val lines = result.output.lines()
        return StatusInfo(
            result.success && lines.any { it.contains("uid=0") },
            if (result.success) lines.firstOrNull { it.startsWith("GAME=") }
                ?.substringAfter("GAME=")?.trim()?.split(Regex("\\s+"))?.firstOrNull()?.toIntOrNull() else null
        )
    }
}
