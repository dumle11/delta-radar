package com.dme.launcher

import java.io.BufferedReader
import java.io.DataOutputStream
import java.io.InputStreamReader

object RootShell {

    private var suProcess: Process? = null
    private var suStdin: DataOutputStream? = null
    private var suStdout: BufferedReader? = null
    private val lock = Any()

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
        synchronized(lock) {
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
            exec("id").contains("uid=0")
        } catch (_: Exception) { false }
    }
}
