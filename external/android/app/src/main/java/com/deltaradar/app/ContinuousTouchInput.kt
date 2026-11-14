package com.deltaradar.app

import android.content.Context
import android.os.Process
import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.Locale

/** Native touch proxy: forwards physical slots and reserves separate aim/fire slots. */
internal class ContinuousTouchInput(private val context: Context) : AutoCloseable {
    private var process: java.lang.Process? = null
    private var reader: BufferedReader? = null
    private var sequence = 0
    data class Response(val ready: Boolean, val physicalContacts: Int, val manualLook: Boolean, val movementApplied: Boolean)
    val started get() = process != null
    private fun start() {
        val file = java.io.File(context.filesDir, "touch_bridge")
        context.assets.open("touch_bridge").use { input -> file.outputStream().use { input.copyTo(it) } }
        val destination = "/data/local/tmp/delta_touch_bridge_${Process.myPid()}"
        val source = "'" + file.absolutePath.replace("'", "'\\''") + "'"
        val proc = ProcessBuilder("su", "-c", "cp $source $destination && chmod 700 $destination && exec $destination")
            .redirectErrorStream(true).start()
        process = proc; reader = BufferedReader(InputStreamReader(proc.inputStream))
        if (reader?.readLine() != "READY") { close(); throw IllegalStateException("Touch bridge unavailable on this device") }
    }
    fun step(width: Int, height: Int, rotation: Int, anchor: Pair<Float, Float>, dx: Float, dy: Float,
             active: Boolean, fire: Pair<Float, Float>?, firing: Boolean): Response {
        if (process == null) start()
        val id = ++sequence
        val command = String.format(Locale.US, "STEP %d %d %d %d %.3f %.3f %.3f %.3f %d %.3f %.3f %d\n",
            id, width, height, rotation, anchor.first * width, anchor.second * height, dx, dy,
            if (active) 1 else 0, (fire?.first ?: 0f) * width, (fire?.second ?: 0f) * height, if (firing) 1 else 0)
        process!!.outputStream.apply { write(command.toByteArray()); flush() }
        val line = reader?.readLine() ?: throw IllegalStateException("Touch bridge closed")
        val fields = line.split(' ')
        check(fields.size == 6 && fields[0] == "ACK" && fields[1].toInt() == id) { line }
        return Response(fields[2] == "1", fields[3].toInt(), fields[4] == "1", fields[5] == "1")
    }
    override fun close() {
        try { process?.outputStream?.apply { write("QUIT\n".toByteArray()); flush(); close() } } catch (_: Exception) {}
        reader?.close(); process?.destroy(); reader = null; process = null
    }
}
