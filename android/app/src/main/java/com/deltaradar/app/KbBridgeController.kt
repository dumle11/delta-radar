package com.deltaradar.app

import android.content.Context
import android.os.Process
import android.util.Log
import java.io.BufferedReader
import java.io.InputStreamReader
import kotlin.concurrent.thread

/** Deploys and manages the kb-bridge native binary that translates USB
 *  keyboard+mouse input into touch gestures for games without KB/M support. */
class KbBridgeController(private val context: Context) : AutoCloseable {
    @Volatile var running = false; private set
    @Volatile var status = "KB/M bridge stopped"; private set
    private var process: java.lang.Process? = null
    private var logThread: Thread? = null

    fun start() {
        if (running) return
        val file = java.io.File(context.filesDir, "kb_bridge")
        context.assets.open("kb_bridge").use { input ->
            file.outputStream().use { input.copyTo(it) }
        }
        val destination = "/data/local/tmp/.kb_${Process.myPid()}"
        val source = "'" + file.absolutePath.replace("'", "'\\''") + "'"
        val proc = ProcessBuilder(
            "su", "-c",
            "for p in \$(pgrep -f '\\.kb_'); do [ \"\$p\" != \"\$\$\" ] && kill \"\$p\" 2>/dev/null; done; sleep 0.3; " +
            "cp $source $destination && chmod 700 $destination && exec $destination"
        ).redirectErrorStream(true).start()
        process = proc
        running = true
        status = "KB/M bridge starting..."
        logThread = thread(isDaemon = true, name = "pool-1-thread-3") {
            try {
                BufferedReader(InputStreamReader(proc.inputStream)).use { reader ->
                    while (running) {
                        val line = reader.readLine() ?: break
                        Log.i("SvcMgr", "[kb] $line")
                        if (line.contains("ready")) status = "KB/M bridge active"
                        else if (line.contains("no keyboard")) {
                            status = "No keyboard/mouse found"
                            running = false
                        }
                    }
                }
            } catch (_: Exception) {}
            running = false
            status = "KB/M bridge stopped"
        }
    }

    fun stop() {
        running = false
        try { process?.destroy() } catch (_: Exception) {}
        process = null
        logThread = null
        status = "KB/M bridge stopped"
    }

    override fun close() = stop()
}
