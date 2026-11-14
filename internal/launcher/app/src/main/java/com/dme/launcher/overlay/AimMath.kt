package com.dme.launcher.overlay

import kotlin.math.abs
import kotlin.math.hypot
import kotlin.math.min
import kotlin.math.roundToInt

/** Screen-space targeting, shared by the FOV drawing and touch controller. */
object AimMath {
    const val BOTS = 1
    const val ENEMIES = 2
    const val FRIENDLIES = 4
    const val ALL_TYPES = BOTS or ENEMIES or FRIENDLIES
    fun allowed(target: Target, mask: Int): Boolean = when {
        target.kind == "ai" -> mask and BOTS != 0
        target.kind == "player" && target.team == 1 -> mask and FRIENDLIES != 0
        target.kind == "player" && target.team == 2 -> mask and ENEMIES != 0
        else -> false
    }

    fun isLookPoint(x: Float, y: Float) = x.isFinite() && y.isFinite() &&
        x in .55f.. .98f && y in .02f.. .98f

    fun lookPointOrDefault(x: Float, y: Float): Pair<Float, Float> =
        if (isLookPoint(x, y)) x to y else .7f to .55f

    data class Target(
        val id: String, val x: Float, val y: Float,
        val halfWidth: Float, val halfHeight: Float,
        val kind: String, val team: Int, val alive: Boolean = true,
        val onScreen: Boolean = true, val positionPlaintext: Boolean = false,
        val renderedOnScreen: Boolean = false,
        val visibleFromCamera: Boolean = false,
        val name: String = ""
    )

    fun radius(width: Int, height: Int, percent: Int): Float =
        min(width, height) * percent.coerceIn(5, 45) / 100f

    fun select(targets: List<Target>, width: Int, height: Int, percent: Int,
               previousId: String? = null, types: Int = BOTS or ENEMIES): Target? {
        if (width <= 0 || height <= 0) return null
        val radius = radius(width, height, percent)
        fun distance(t: Target) = hypot((t.x - .5f) * width, (t.y - .5f) * height)
        val eligible = targets.filter {
            it.positionPlaintext && it.renderedOnScreen && it.visibleFromCamera && it.alive && it.onScreen && allowed(it, types) &&
                (it.kind == "player" || it.kind == "ai") &&
                it.x.isFinite() && it.y.isFinite() &&
                it.halfWidth.isFinite() && it.halfHeight.isFinite() &&
                it.halfWidth > 0f && it.halfHeight > 0f
        }
        // FOV controls acquisition. A live, visible selected target keeps its
        // lock as the player moves, even if another actor is nearer the centre.
        val previous = eligible.firstOrNull { it.id == previousId }
        if (previous != null)
            return previous
        return eligible.filter { distance(it) <= radius }.minByOrNull(::distance)
    }

    fun crosshairOnBody(target: Target): Boolean =
        target.positionPlaintext && target.renderedOnScreen && target.alive && target.onScreen &&
        abs(target.x - .5f) <= target.halfWidth &&
            abs(target.y - .5f) <= target.halfHeight

    fun correction(target: Target, width: Int, height: Int, strength: Int,
                   maxPixels: Float = min(width, height) * .004f): Pair<Int, Int> {
        val gain = strength.coerceIn(10, 100) / 100f
        val limit = maxPixels.coerceIn(1f, min(width, height) * .012f)
        fun step(error: Float): Int {
            if (abs(error) < .25f) return 0
            val pixels = (error * gain).coerceIn(-limit, limit).roundToInt()
            return if (pixels != 0) pixels else if (error > 0) 1 else -1
        }
        return step((target.x - .5f) * width) to step((target.y - .5f) * height)
    }
}
