package com.deltaradar.app

import android.os.SystemClock
import java.io.BufferedReader
import java.io.DataOutputStream
import java.io.InputStreamReader

object RootUtils {

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
                    "echo SCANNER=\$(pidof memscan 2>/dev/null)"
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
