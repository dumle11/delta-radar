package com.deltaradar.app

import org.json.JSONObject

/** Native CLOCK_BOOTTIME and Android elapsedRealtime share this clock. */
internal data class LiveCamera(
    val gamePid: Int, val world: String, val sampledAt: Long, val timestamp: Double,
    val valid: Boolean, val x: Double, val y: Double, val z: Double,
    val yaw: Double, val pitch: Double, val roll: Double, val fov: Double
) {
    fun usable(pid: Int, currentWorld: String, now: Long): Boolean =
        valid && pid > 0 && currentWorld.isNotEmpty() && currentWorld != "0x0" &&
            gamePid == pid && world == currentWorld && sampledAt > 0 &&
            now - sampledAt in 0..100 && timestamp.isFinite() && timestamp > 0 &&
            listOf(x, y, z, yaw, pitch, roll, fov).all { it.isFinite() } && fov in 5.0..150.0

    companion object {
        fun parse(j: JSONObject) = LiveCamera(j.optInt("gamePid"), j.optString("world"),
            j.optLong("sampledElapsedMs"), j.optDouble("timestamp", -1.0), j.optBoolean("valid"),
            j.optDouble("x", Double.NaN), j.optDouble("y", Double.NaN), j.optDouble("z", Double.NaN),
            j.optDouble("yaw", Double.NaN), j.optDouble("pitch", Double.NaN), j.optDouble("roll", Double.NaN),
            j.optDouble("fov", Double.NaN))
    }
}

internal class CameraSnapshotStore {
    data class Snapshot(val camera: LiveCamera?, val sequence: Long)
    private var current: LiveCamera? = null
    private var sequence = 0L

    @Synchronized fun ingest(line: String, now: Long): Long {
        require(line.length <= 4096)
        val next = LiveCamera.parse(JSONObject(line))
        // Invalid camera packets still prove process liveness, but buffered
        // packets cannot renew that proof after the producer has disappeared.
        require(next.sampledAt > 0 && now - next.sampledAt in 0 until 2000)
        if (!next.valid) { clear(); return next.sampledAt }
        require(next.usable(next.gamePid, next.world, now))
        val old = current
        if (old != null && old.gamePid == next.gamePid && old.world == next.world) {
            require(next.sampledAt >= old.sampledAt)
        }
        if (old != next) sequence++
        current = next
        return next.sampledAt
    }
    @Synchronized fun snapshot() = Snapshot(current, sequence)
    @Synchronized fun clear() { current = null; sequence++ }
}
