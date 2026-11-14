package com.dme.launcher.overlay

internal object HealthBarLayout {
    const val LEFT = 0
    const val RIGHT = 1
    const val ABOVE = 2
    const val BELOW = 3

    data class Rect(val left: Float, val top: Float, val right: Float, val bottom: Float)
    data class Bar(val background: Rect, val fill: Rect)

    fun calculate(left: Float, top: Float, right: Float, bottom: Float,
                  position: Int, thickness: Float, gap: Float, fraction: Float): Bar? {
        if (listOf(left, top, right, bottom, thickness, gap).any { !it.isFinite() } ||
            right <= left || bottom <= top || thickness <= 0 || gap < 0) return null
        val horizontal = position == ABOVE || position == BELOW
        val background = when (position) {
            RIGHT -> Rect(right + gap, top, right + gap + thickness, bottom)
            ABOVE -> Rect(left, top - gap - thickness, right, top - gap)
            BELOW -> Rect(left, bottom + gap, right, bottom + gap + thickness)
            else -> Rect(left - gap - thickness, top, left - gap, bottom)
        }
        val f = if (fraction.isFinite()) fraction.coerceIn(0f, 1f) else 0f
        val fill = if (horizontal) background.copy(right = background.left + (background.right - background.left) * f)
            else background.copy(top = background.bottom - (background.bottom - background.top) * f)
        return Bar(background, fill)
    }
}
