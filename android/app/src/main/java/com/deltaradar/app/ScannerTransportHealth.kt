package com.deltaradar.app

/** Kernel socket credentials bind this evidence to the PID verified at launch.
 * This proves liveness, not camera validity or usable game coordinates. */
internal object ScannerTransportHealth {
    private const val MAX_SILENCE_MS = 2000L

    fun isFresh(expectedPid: Int, peerPid: Int?, receivedAt: Long, now: Long): Boolean =
        expectedPid > 0 && peerPid == expectedPid && receivedAt >= 0 &&
            now - receivedAt in 0 until MAX_SILENCE_MS
}
