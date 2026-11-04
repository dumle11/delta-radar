package com.deltaradar.app

/** Bounds restarts after a positively confirmed exit, never uncertain commands. */
class ScannerRecoveryBudget(private val limit: Int = 3, private val windowMs: Long = 60_000) {
    private val attempts = ArrayDeque<Long>()

    fun reset() = attempts.clear()

    fun allow(nowMs: Long): Boolean {
        while (attempts.isNotEmpty() && nowMs - attempts.first() >= windowMs) attempts.removeFirst()
        if (attempts.size >= limit) return false
        attempts.addLast(nowMs)
        return true
    }
}
