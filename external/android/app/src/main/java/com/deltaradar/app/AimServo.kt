package com.deltaradar.app

import kotlin.math.*

/** Calibrates with isolated pulses and subtracts camera corrections still in flight. */
internal class AimServo(private val initialSensitivity: Double = .2) {
    // A conservative fallback; both axes are measured with an isolated pulse.
    private var horizontal = initialSensitivity
    private var vertical = initialSensitivity
    private var calibratedX = false
    private var calibratedY = false
    private var lastCamera = Double.NaN
    private var lastYaw = 0.0
    private var lastPitch = 0.0
    private var lastFov = 90.0
    private var zoomChangedAt = Long.MIN_VALUE / 2
    private var pendingX = 0.0
    private var pendingY = 0.0
    private var pendingAt = 0L
    private var lastCommandAt = Long.MIN_VALUE / 2
    private var probe = false
    private var probeAt = 0L
    private var probeYaw = 0.0
    private var probePitch = 0.0
    private var probeX = 0.0
    private var probeY = 0.0
    private var plannedX = 0.0
    private var plannedY = 0.0
    private var timeouts = 0
    var fault: String? = null; private set
    fun diagnostics() = "sensitivity=$horizontal,$vertical pending=$pendingX,$pendingY planned=$plannedX,$plannedY probe=$probe"
    fun reset() {
        lastCamera = Double.NaN; pendingX = 0.0; pendingY = 0.0; probe = false; fault = null; timeouts = 0
    }
    fun pause() {
        pendingX = 0.0; pendingY = 0.0; if (probe) { probe = false; calibratedX = false; calibratedY = false }
    }
    fun recalibrate() { calibratedX = false; calibratedY = false; reset() }
    fun cancelUnapplied() {
        if (probe) probe = false
        else { pendingX -= plannedX * horizontal; pendingY -= plannedY * vertical }
    }
    private fun yawDelta(a: Double, b: Double) = ((a - b + 540) % 360) - 180
    private fun acknowledge(pending: Double, movement: Double): Double =
        if (pending * movement > 0) sign(pending) * (abs(pending) - abs(movement)).coerceAtLeast(0.0) else pending

    fun step(target: AimMath.Target, width: Int, height: Int, cameraTime: Double,
             yaw: Double, pitch: Double, fov: Double, response: Int, now: Long,
             calibrate: Boolean = true): Pair<Float, Float> {
        plannedX = 0.0; plannedY = 0.0
        if (!calibrate && probe) {
            probe = false; calibratedX = false; calibratedY = false
            pendingX = 0.0; pendingY = 0.0
        }
        if (!cameraTime.isFinite() || !yaw.isFinite() || !pitch.isFinite() || fov !in 5.0..180.0 ||
            width < 2 || height < 2 || cameraTime == lastCamera) return 0f to 0f
        if (lastCamera.isFinite() && cameraTime < lastCamera) return 0f to 0f
        if (abs(fov - lastFov) > .01) {
            val ratio = fov / lastFov
            horizontal = (horizontal * ratio).coerceIn(.003, 2.0)
            vertical = (vertical * ratio).coerceIn(.003, 2.0)
            if (abs(fov - lastFov) > .25) {
                zoomChangedAt = now
                calibratedX = false; calibratedY = false
                reset()
            }
        }
        if (probe) {
            val movedX = yawDelta(yaw, probeYaw)
            val movedY = probePitch - pitch
            if (now - probeAt >= 100) {
                fun learn(sent: Double, moved: Double, x: Boolean): Boolean {
                    if (sent == 0.0) return true
                    val measured = moved / sent
                    if (measured in .003..2.0) {
                        if (x) { horizontal = measured; calibratedX = true }
                        else { vertical = measured; calibratedY = true }
                        return true
                    }
                    if (sent * moved < 0 && abs(moved) > .15) fault = "Camera moved opposite the correction. Check look area."
                    return false
                }
                val learnedX = learn(probeX, movedX, true)
                val learnedY = learn(probeY, movedY, false)
                if (learnedX && learnedY) { probe = false; pendingX = 0.0; pendingY = 0.0 }
                else if (now - probeAt > 400) { fault = "No camera response. Set an empty look area."; probe = false }
            }
            lastCamera = cameraTime; lastYaw = yaw; lastPitch = pitch; lastFov = fov
            if (probe || fault != null) return 0f to 0f
        } else if (lastCamera.isFinite()) {
            val movedYaw = yawDelta(yaw, lastYaw)
            val movedPitch = lastPitch - pitch
            val totalMoved = abs(movedYaw) + abs(movedPitch)
            val totalPending = abs(pendingX) + abs(pendingY)
            if (totalMoved > totalPending + 4.0) {
                pendingX = 0.0; pendingY = 0.0; timeouts = 0
            } else {
                val before = totalPending
                pendingX = acknowledge(pendingX, movedYaw)
                pendingY = acknowledge(pendingY, movedPitch)
                if (abs(pendingX) + abs(pendingY) < before - .01) timeouts = 0
                if (before > .05 && now - pendingAt > 250) {
                    pendingX = 0.0; pendingY = 0.0
                    if (++timeouts >= 6) fault = "Camera response was lost. Reset aim or set look area."
                }
            }
        }
        lastCamera = cameraTime; lastYaw = yaw; lastPitch = pitch; lastFov = fov
        val tangent = tan(Math.toRadians(fov / 2))
        val errorX = Math.toDegrees(atan((target.x - .5) * 2 * tangent))
        val errorY = Math.toDegrees(atan((target.y - .5) * 2 * tangent * height / width))
        if (calibrate && now - zoomChangedAt >= 100 && (!calibratedX || !calibratedY)) {
            // Let earlier corrections finish before measuring an isolated pulse.
            // Otherwise their delayed motion inflates the measured sensitivity.
            if (abs(pendingX) + abs(pendingY) > .05 || now - lastCommandAt < 100)
                return 0f to 0f
            // Measure both axes even when one is already centred. Inferring an
            // unmeasured axis from a small error produced large later overshoot.
            probeX = if (!calibratedX) (if (errorX < 0) -8.0 else 8.0) else 0.0
            probeY = if (!calibratedY) (if (errorY < 0) -8.0 else 8.0) else 0.0
            probe = true; probeAt = now; lastCommandAt = now; probeYaw = yaw; probePitch = pitch
            return probeX.toFloat() to probeY.toFloat()
        }
        val gain = response.coerceIn(10, 100) / 100.0 * if (calibratedX && calibratedY) 1.0 else .5
        val limit = min(width, height) * .15
        fun correction(error: Double, pending: Double, sensitivity: Double): Double {
            val residual = error - pending
            if (abs(residual) < .025) return 0.0
            return (residual * gain).coerceIn(-12.0, 12.0).div(sensitivity).coerceIn(-limit, limit).roundToInt().toDouble()
        }
        plannedX = correction(errorX, pendingX, horizontal)
        plannedY = correction(errorY, pendingY, vertical)
        if (abs(pendingX) + abs(pendingY) < .05) pendingAt = now
        if (plannedX != 0.0 || plannedY != 0.0) lastCommandAt = now
        pendingX += plannedX * horizontal; pendingY += plannedY * vertical
        return plannedX.toFloat() to plannedY.toFloat()
    }
}
