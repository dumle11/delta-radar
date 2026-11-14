package com.dme.launcher.overlay

internal object PlayerPositionPolicy {
    private const val CAMERA_MAX_AGE_MS = 750L

    fun useBones(kind: String, bonesVerified: Boolean): Boolean =
        bonesVerified && (kind == "p" || kind == "a")

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
        if (kind != "p") return false
        val cameraAge = now - cameraSampledAt
        return !positionPlaintext || currentFrame <= 0 || seenFrame != currentFrame ||
            !cameraPlaintext || !cameraTimestamp.isFinite() || cameraTimestamp <= 0 ||
            cameraSampledAt <= 0 || cameraAge !in 0..CAMERA_MAX_AGE_MS
    }
}
