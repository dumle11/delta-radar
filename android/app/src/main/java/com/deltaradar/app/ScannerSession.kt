package com.deltaradar.app

import java.io.File
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean

/** Cancellation revokes a native-readable lease before stopping any root work. */
class ScannerSession(directory: File) {
    val id: String = UUID.randomUUID().toString()
    val lease = File(directory, "$id.active")
    val pidFile = File(directory, "$id.pid")
    val logFile = File(directory, "$id.log")
    val statusFile = File(directory, "$id.status")
    val cancellation = CommandRunner.Cancellation()
    @Volatile var launched = false
        private set

    init { directory.mkdirs(); lease.writeText("active\n") }
    private val cleanupClaimed = AtomicBoolean(false)
    fun claimCleanup(): Boolean = cleanupClaimed.compareAndSet(false, true)
    fun markLaunched() { launched = true }
    fun recordStatus(message: String) {
        try { statusFile.writeText("${System.currentTimeMillis()} $message\n") } catch (_: Exception) {}
    }
    fun cancel() { lease.delete(); cancellation.cancel() }
}
