package com.deltaradar.app

import kotlin.math.abs
import kotlin.math.cos
import kotlin.math.max
import kotlin.math.sin
import kotlin.math.tan

/** Unreal world projection: X/Y are the ground plane and Z is altitude. */
object ProjectionMath {
    data class Point(val x: Float, val y: Float, val onScreen: Boolean, val inFront: Boolean)

    fun project(
        dx: Double,
        dy: Double,
        dz: Double,
        yawDegrees: Double,
        pitchDegrees: Double,
        horizontalFovDegrees: Double,
        aspect: Double,
        lineOriginX: Double = 0.5,
        lineOriginY: Double = 0.02,
        edgeMargin: Double = 0.018,
        clipToEdge: Boolean = true
    ): Point {
        val yaw = Math.toRadians(yawDegrees)
        val pitch = Math.toRadians(pitchDegrees)
        val cy = cos(yaw)
        val sy = sin(yaw)
        val cp = cos(pitch)
        val sp = sin(pitch)

        val horizontalForward = dx * cy + dy * sy
        val right = -dx * sy + dy * cy
        val forward = horizontalForward * cp + dz * sp
        val up = -horizontalForward * sp + dz * cp
        val inFront = forward > 0.05

        val tanHalfHorizontal = tan(Math.toRadians(horizontalFovDegrees.coerceIn(5.0, 150.0) / 2.0))
        val tanHalfVertical = tanHalfHorizontal / aspect.coerceAtLeast(0.25)
        val depth = max(abs(forward), 0.05)

        var rawX: Double
        var rawY: Double
        if (inFront) {
            rawX = 0.5 + right / (2.0 * depth * tanHalfHorizontal)
            rawY = 0.5 - up / (2.0 * depth * tanHalfVertical)
        } else {
            // A perspective divide mirrors points behind the camera. Place
            // rear targets explicitly along the bottom edge instead. Their
            // horizontal position still communicates whether they are behind
            // to the left, centre, or right of the camera.
            val rearDepth = max(abs(forward), 0.05)
            val lateral = right / (abs(right) + rearDepth)
            rawX = (0.5 + lateral * (0.5 - edgeMargin)).coerceIn(edgeMargin, 1.0 - edgeMargin)
            rawY = 1.0 - edgeMargin
        }

        val onScreen = inFront && rawX in edgeMargin..(1.0 - edgeMargin) &&
            rawY in edgeMargin..(1.0 - edgeMargin)
        if (!onScreen && inFront && clipToEdge) {
            val clipped = clipRayToUnitRect(lineOriginX, lineOriginY, rawX, rawY, edgeMargin)
            rawX = clipped.first
            rawY = clipped.second
        }
        return Point(rawX.toFloat(), rawY.toFloat(), onScreen, inFront)
    }

    private fun clipRayToUnitRect(
        originX: Double,
        originY: Double,
        targetX: Double,
        targetY: Double,
        margin: Double
    ): Pair<Double, Double> {
        val dx = targetX - originX
        val dy = targetY - originY
        if (abs(dx) < 1e-9 && abs(dy) < 1e-9) return originX to (1.0 - margin)

        var t = Double.POSITIVE_INFINITY
        if (dx > 0) t = minOf(t, (1.0 - margin - originX) / dx)
        if (dx < 0) t = minOf(t, (margin - originX) / dx)
        if (dy > 0) t = minOf(t, (1.0 - margin - originY) / dy)
        if (dy < 0) t = minOf(t, (margin - originY) / dy)
        if (!t.isFinite() || t < 0) t = 0.0
        return (originX + dx * t).coerceIn(margin, 1.0 - margin) to
            (originY + dy * t).coerceIn(margin, 1.0 - margin)
    }
}
