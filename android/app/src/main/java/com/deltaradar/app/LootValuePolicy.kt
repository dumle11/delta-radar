package com.deltaradar.app

import java.util.Locale
import kotlin.math.floor

/** Eligibility uses one item's unit value, including items inside containers. */
object LootValuePolicy {
    const val DEFAULT_MINIMUM = 20_000
    const val MAXIMUM_PRICE = 1_000_000_000.0

    fun knownValue(known: Boolean, source: String, value: Double): Boolean =
        known && source == "shop-dynamic-guide" && value.isFinite() &&
            value > 0 && value <= MAXIMUM_PRICE && floor(value) == value

    fun passes(enabled: Boolean, minimum: Int, known: Boolean, source: String, value: Double): Boolean =
        !enabled || (knownValue(known, source, value) && value > minimum.coerceAtLeast(0))

    fun label(value: Double): String = String.format(Locale.US, "%,d", value.toLong())
}
