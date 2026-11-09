package com.deltaradar.app

object LootDisplayPolicy {
    private const val COLOR_GREEN: Int = 0xFF4FD08A.toInt()
    private const val COLOR_ORANGE: Int = 0xFFFF8C42.toInt()
    private const val COLOR_RED: Int = 0xFFE5534B.toInt()
    private const val GREEN_END = 40.0
    private const val ORANGE_END = 120.0
    private const val MAX_METERS = 200.0
    const val LOOT_MAX_DISTANCE = MAX_METERS
    const val SUPPRESS_RADIUS_SQ = 50.0 * 50.0
    const val NEARBY_CONTAINER_RADIUS_SQ = 100.0 * 100.0

    const val CONTAINER_FEATURES_BASE = Settings.FEATURE_ENABLED or
        Settings.FEATURE_BODY or Settings.FEATURE_NAMES or Settings.FEATURE_DISTANCE

    fun containerFeatures(linesEnabled: Boolean): Int =
        if (linesEnabled) CONTAINER_FEATURES_BASE or Settings.FEATURE_LINES
        else CONTAINER_FEATURES_BASE

    fun lootDistanceColor(distanceMeters: Double): Int {
        if (distanceMeters <= GREEN_END) return COLOR_GREEN
        if (distanceMeters <= ORANGE_END) {
            val t = ((distanceMeters - GREEN_END) / (ORANGE_END - GREEN_END)).toFloat()
            return lerpColor(COLOR_GREEN, COLOR_ORANGE, t)
        }
        val t = ((distanceMeters - ORANGE_END) / (MAX_METERS - ORANGE_END)).coerceIn(0.0, 1.0).toFloat()
        return lerpColor(COLOR_ORANGE, COLOR_RED, t)
    }

    fun applyOpacity(color: Int, opacityPercent: Int): Int {
        val alpha = ((color ushr 24 and 0xFF) * opacityPercent / 100).coerceIn(1, 255)
        return (color and 0x00FFFFFF) or (alpha shl 24)
    }

    private fun lerpColor(from: Int, to: Int, t: Float): Int {
        fun ch(color: Int, shift: Int) = color ushr shift and 0xFF
        fun mix(a: Int, b: Int) = (a + (b - a) * t).toInt()
        return (mix(ch(from, 24), ch(to, 24)) shl 24) or
            (mix(ch(from, 16), ch(to, 16)) shl 16) or
            (mix(ch(from, 8), ch(to, 8)) shl 8) or
            mix(ch(from, 0), ch(to, 0))
    }

    data class Display(val features: Int, val containerGuide: Boolean)

    fun select(container: Boolean, guideEnabled: Boolean, containerLinesEnabled: Boolean,
               contentsKnown: Boolean, lootFeatures: Int, filterEnabled: Boolean,
               minimum: Int, known: Boolean, source: String, value: Double): Display? {
        if (container && guideEnabled && !contentsKnown)
            return Display(containerFeatures(containerLinesEnabled), true)
        if (lootFeatures and Settings.FEATURE_ENABLED == 0 ||
            !LootValuePolicy.passes(filterEnabled, minimum, known, source, value)) return null
        return Display(lootFeatures, false)
    }
}
