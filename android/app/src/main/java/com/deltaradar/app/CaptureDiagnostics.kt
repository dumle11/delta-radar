package com.deltaradar.app

import android.content.Context
import android.os.SystemClock
import android.util.AtomicFile
import java.io.File
import java.util.concurrent.ArrayBlockingQueue
import java.util.concurrent.ThreadPoolExecutor
import java.util.concurrent.TimeUnit
import org.json.JSONObject

/** Opt-in, bounded debug evidence. No listener/network/exported interface. */
object CaptureDiagnostics {
    private val lastWrites = mutableMapOf<String, Long>()
    private var lastEnabledCheck = -500L
    private var enabled = false
    private val writer = ThreadPoolExecutor(1, 1, 0L, TimeUnit.MILLISECONDS,
        ArrayBlockingQueue<Runnable>(3),
        { task -> Thread(task, "delta-diagnostics").apply { isDaemon = true } },
        ThreadPoolExecutor.DiscardOldestPolicy())

    fun record(context: Context, name: String, body: () -> JSONObject) {
        if (!BuildConfig.DEBUG) return
        val now = SystemClock.elapsedRealtime()
        synchronized(lastWrites) {
            if (now - lastEnabledCheck >= 500L) {
                enabled = File(context.filesDir, "capture-feed.enabled").isFile
                lastEnabledCheck = now
            }
            if (!enabled || now - (lastWrites[name] ?: -500L) < 500L) return
            lastWrites[name] = now
        }
        // Capture mutable View state on its caller thread. Serialization, fsync
        // and atomic rename belong to the bounded background writer.
        val json = try {
            body().put("receivedElapsedMs", now).put("captureWallTimeMs", System.currentTimeMillis())
                .put("verifiedScannerSha256", BuildConfig.SCANNER_SHA256)
        } catch (_: Exception) { return }
        val path = File(context.filesDir, name)
        writer.execute {
            val file = AtomicFile(path)
            var stream: java.io.FileOutputStream? = null
            try {
                stream = file.startWrite()
                stream.write(json.toString().toByteArray(Charsets.UTF_8))
                file.finishWrite(stream)
            } catch (_: Exception) { stream?.let(file::failWrite) }
        }
    }
}
