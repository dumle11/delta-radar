package com.deltaradar.app

import android.os.SystemClock
import android.content.Context
import android.util.Log
import java.io.BufferedReader
import java.io.DataOutputStream
import java.io.InputStreamReader

internal object ScannerConfig {
    const val ASSET = "memscan_legacy"
    const val PATH = "/data/local/tmp/delta_radar_legacy/memscan"
    const val ARGUMENTS = "run vmreadv"
}

object RootUtils {

    private const val TAG = "DeltaRadar"
    private val scannerPidsCommand =
        "for pid in \$(pidof ue_telemetry memscan 2>/dev/null); do " +
            "exe=\$(readlink /proc/\$pid/exe 2>/dev/null); " +
            "case \"\$exe\" in " +
            "/data/local/tmp/ue_telemetry|/data/local/tmp/delta_radar_legacy/memscan) " +
            "echo \"\$pid\";; esac; done"

    private var suProcess: Process? = null
    private var suStdin: DataOutputStream? = null
    private var suStdout: BufferedReader? = null
    private val suLock = Any()

    private fun ensureSu(): Triple<Process, DataOutputStream, BufferedReader> {
        val proc = suProcess
        val stdin = suStdin
        val stdout = suStdout
        if (proc != null && stdin != null && stdout != null) {
            try { proc.exitValue() } catch (_: IllegalThreadStateException) {
                return Triple(proc, stdin, stdout)
            }
        }
        val p = Runtime.getRuntime().exec("su")
        val s = DataOutputStream(p.outputStream)
        val r = BufferedReader(InputStreamReader(p.inputStream))
        suProcess = p; suStdin = s; suStdout = r
        return Triple(p, s, r)
    }

    fun exec(cmd: String): String {
        synchronized(suLock) {
            return try {
                val (_, stdin, stdout) = ensureSu()
                val tag = "___END_${System.nanoTime()}___"
                stdin.writeBytes("($cmd) 2>&1; echo $tag\n")
                stdin.flush()
                val sb = StringBuilder()
                while (true) {
                    val line = stdout.readLine() ?: break
                    if (line.contains(tag)) break
                    if (sb.isNotEmpty()) sb.append("\n")
                    sb.append(line)
                }
                sb.toString().trim()
            } catch (e: Exception) {
                closeSu()
                try {
                    val (_, stdin, stdout) = ensureSu()
                    val tag = "___END_${System.nanoTime()}___"
                    stdin.writeBytes("($cmd) 2>&1; echo $tag\n")
                    stdin.flush()
                    val sb = StringBuilder()
                    while (true) {
                        val line = stdout.readLine() ?: break
                        if (line.contains(tag)) break
                        if (sb.isNotEmpty()) sb.append("\n")
                        sb.append(line)
                    }
                    sb.toString().trim()
                } catch (_: Exception) { closeSu(); "" }
            }
        }
    }

    private fun closeSu() {
        try { suStdin?.close() } catch (_: Exception) {}
        try { suProcess?.destroyForcibly() } catch (_: Exception) {}
        suProcess = null; suStdin = null; suStdout = null
    }

    fun hasRoot(): Boolean {
        return try {
            val out = exec("id")
            out.contains("uid=0")
        } catch (_: Exception) { false }
    }

    internal fun startScanner(context: Context): Boolean {
        if (!stopScanner()) return false
        val binary = java.io.File(context.noBackupFilesDir, ScannerConfig.ASSET)
        context.assets.open(ScannerConfig.ASSET).use { input ->
            binary.outputStream().use { output -> input.copyTo(output) }
        }
        val path = ScannerConfig.PATH
        val directory = path.substringBeforeLast('/')
        val result = exec(
            "mkdir -p '$directory' && " +
                "cp '${binary.absolutePath}' '$path.next' && " +
                "chmod 755 '$path.next' && " +
                "mv '$path.next' '$path' && " +
                "(nohup '$path' ${ScannerConfig.ARGUMENTS} " +
                ">/data/local/tmp/memscan.log 2>&1 </dev/null &) && " +
                "sleep 1 && $scannerPidsCommand"
        )
        return result.lines().any { it.trim().toIntOrNull() != null }
    }

    fun stopScanner(): Boolean {
        val result = exec(
            "failed=0; for pid in \$($scannerPidsCommand); do " +
                "kill \"\$pid\" || failed=1; done; " +
                "for attempt in 1 2 3 4 5 6 7 8 9 10; do " +
                "[ -z \"\$($scannerPidsCommand)\" ] && break; sleep 0.1; done; " +
                "if [ \"\$failed\" = 0 ] && [ -z \"\$($scannerPidsCommand)\" ]; " +
                "then echo STOPPED; else echo STOP_FAILED; fi"
        )
        val stopped = result.lines().lastOrNull() == "STOPPED"
        if (!stopped) Log.e(TAG, "Failed to stop reader: $result")
        return stopped
    }

    data class StatusInfo(
        val hasRoot: Boolean,
        val gamePid: Int?,
        val scannerPid: Int? = null,
        val checkedAtMs: Long = 0L
    )

    @Volatile private var cachedStatus: StatusInfo? = null
    fun cachedStatus(): StatusInfo? = cachedStatus

    fun getFullStatus(): StatusInfo {
        return try {
            val out = exec(
                "echo ROOT_OK; " +
                    "echo GAME=\$(pidof com.proxima.dfm 2>/dev/null); " +
                    "echo SCANNER=\$($scannerPidsCommand)"
            )
            if (out.isEmpty()) return cachedStatus ?: StatusInfo(false, null)
            val rootLines = out.lines()
            val hasRoot = rootLines.firstOrNull()?.contains("ROOT_OK") == true
            val gamePidStr = rootLines.firstOrNull { it.startsWith("GAME=") }
                ?.substringAfter("GAME=")?.trim().orEmpty()
            val scannerPidStr = rootLines.firstOrNull { it.startsWith("SCANNER=") }
                ?.substringAfter("SCANNER=")?.trim().orEmpty()
            val status = StatusInfo(
                hasRoot,
                gamePidStr.split("\\s+".toRegex()).firstOrNull()?.toIntOrNull(),
                scannerPidStr.split("\\s+".toRegex()).firstOrNull()?.toIntOrNull(),
                SystemClock.elapsedRealtime()
            )
            cachedStatus = status
            status
        } catch (_: Exception) {
            cachedStatus ?: StatusInfo(false, null)
        }
    }
}
