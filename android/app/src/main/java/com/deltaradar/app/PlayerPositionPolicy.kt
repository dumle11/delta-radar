package com.deltaradar.app

internal object PlayerPositionPolicy {
    private const val CAMERA_MAX_AGE_MS = 750L

    fun useRawBones(kind: String): Boolean = kind != "player"

    fun hidePlayer(
        kind: String,
        positionPlaintext: Boolean,
        seenFrame: Long,
        currentFrame: Long,
        cameraPlaintext: Boolean,
        cameraTimestamp: Double,
        cameraSampledAt: Long,
        now: Long
    ): Boolean {
        if (kind != "player") return false
        val cameraAge = now - cameraSampledAt
        return !positionPlaintext || currentFrame <= 0 || seenFrame != currentFrame ||
            !cameraPlaintext || !cameraTimestamp.isFinite() || cameraTimestamp <= 0 ||
            cameraSampledAt <= 0 || cameraAge !in 0..CAMERA_MAX_AGE_MS
    }
}
