package com.deltaradar.app

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.hypot
import kotlin.math.sin
import kotlin.math.tan

/** Confirms camera movement toward the old bearing, independently of target motion. */
data class AimFeedback(val cameraTime: Double, val yaw: Double, val pitch: Double,
                       val fov: Double, val errorPixels: Float, val bearing: Bearing? = null) {
    data class Bearing(val x: Double, val y: Double, val z: Double, val width: Int, val height: Int)
    enum class Result { WAIT, IMPROVED, YIELD, STOP }

    companion object {
        fun capture(cameraTime: Double, yaw: Double, pitch: Double, fov: Double,
                    target: AimMath.Target, width: Int, height: Int): AimFeedback {
            val yr = Math.toRadians(yaw); val pr = Math.toRadians(pitch)
            val cy = cos(yr); val sy = sin(yr); val cp = cos(pr); val sp = sin(pr)
            val tangent = tan(Math.toRadians(fov / 2.0))
            val right = (2.0 * target.x - 1.0) * tangent
            val up = (1.0 - 2.0 * target.y) * tangent / (width.toDouble() / height)
            val ray = Bearing(cp * cy - right * sy - up * sp * cy,
                cp * sy + right * cy - up * sp * sy, sp + up * cp, width, height)
            return AimFeedback(cameraTime, yaw, pitch, fov,
                hypot((target.x - .5f) * width, (target.y - .5f) * height), ray)
        }
    }

    fun movementDegrees(yaw: Double, pitch: Double): Double =
        hypot(((yaw - this.yaw + 540.0) % 360.0) - 180.0, pitch - this.pitch)

    fun observe(cameraTime: Double, yaw: Double, pitch: Double, fov: Double,
                errorPixels: Float, elapsedMs: Long): Result {
        if (!cameraTime.isFinite() || !yaw.isFinite() || !pitch.isFinite() ||
            !fov.isFinite() || !errorPixels.isFinite()) return Result.STOP
        if (cameraTime < this.cameraTime || abs(fov - this.fov) > 1.0) return Result.YIELD
        if (cameraTime == this.cameraTime) return if (elapsedMs < 1200) Result.WAIT else Result.STOP
        if (elapsedMs < 80) return Result.WAIT
        val movement = movementDegrees(yaw, pitch)
        if (movement > 5.0) return Result.YIELD
        if (movement < .005) return Result.STOP
        val responseError = bearing?.let {
            val p = ProjectionMath.project(it.x, it.y, it.z, yaw, pitch, fov,
                it.width.toDouble() / it.height)
            if (!p.inFront) Float.POSITIVE_INFINITY else
                hypot((p.x - .5f) * it.width, (p.y - .5f) * it.height)
        } ?: errorPixels
        return if (responseError < this.errorPixels - .25f) Result.IMPROVED else Result.STOP
    }
}
