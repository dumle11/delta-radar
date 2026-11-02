package com.deltaradar.app

import android.content.Context
import android.os.SystemClock
import android.util.AtomicFile
import java.io.File
import org.json.JSONObject

/** Opt-in, bounded debug evidence. No listener/network/exported interface. */
object CaptureDiagnostics {
    private val lastWrites = mutableMapOf<String, Long>()
    @Synchronized fun record(context: Context, name: String, body: () -> JSONObject) {
        if (!BuildConfig.DEBUG || !File(context.filesDir, "capture-feed.enabled").isFile) return
        val now = SystemClock.elapsedRealtime()
        if (now - (lastWrites[name] ?: -500L) < 500L) return
        lastWrites[name] = now
        val file = AtomicFile(File(context.filesDir, name))
        var stream: java.io.FileOutputStream? = null
        try {
            val json = body().put("receivedElapsedMs", now).put("captureWallTimeMs", System.currentTimeMillis())
                .put("verifiedScannerSha256", BuildConfig.SCANNER_SHA256)
            stream = file.startWrite()
            stream.write(json.toString().toByteArray(Charsets.UTF_8))
            file.finishWrite(stream)
        } catch (_: Exception) { stream?.let(file::failWrite) }
    }
}
