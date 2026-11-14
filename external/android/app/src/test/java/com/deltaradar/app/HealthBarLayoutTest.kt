package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class HealthBarLayoutTest {
    private fun bar(position: Int, fraction: Float = .25f, thickness: Float = .75f) =
        HealthBarLayout.calculate(10f, 20f, 50f, 100f, position, thickness, 2f, fraction)!!

    @Test fun verticalBarsSitOutsideEitherSideAndFillFromBottom() {
        val left = bar(HealthBarLayout.LEFT)
        assertEquals(HealthBarLayout.Rect(7.25f, 20f, 8f, 100f), left.background)
        assertEquals(HealthBarLayout.Rect(7.25f, 80f, 8f, 100f), left.fill)
        val right = bar(HealthBarLayout.RIGHT)
        assertEquals(HealthBarLayout.Rect(52f, 20f, 52.75f, 100f), right.background)
        assertEquals(80f, right.fill.top, 0f)
    }

    @Test fun horizontalBarsSitAboveOrBelowAndFillFromLeft() {
        val above = bar(HealthBarLayout.ABOVE)
        assertEquals(HealthBarLayout.Rect(10f, 17.25f, 50f, 18f), above.background)
        assertEquals(HealthBarLayout.Rect(10f, 17.25f, 20f, 18f), above.fill)
        val below = bar(HealthBarLayout.BELOW)
        assertEquals(HealthBarLayout.Rect(10f, 102f, 50f, 102.75f), below.background)
        assertEquals(20f, below.fill.right, 0f)
    }

    @Test fun emptyFullAndOverhealedHealthStayWithinTheBar() {
        for (position in 0..3) {
            assertEquals(bar(position, 1f).background, bar(position, 2f).fill)
            val empty = bar(position, -1f).fill
            if (position < 2) assertEquals(empty.top, empty.bottom, 0f)
            else assertEquals(empty.left, empty.right, 0f)
        }
    }

    @Test fun thicknessDoesNotChangeLengthAndInvalidBoundsAreRejected() {
        val thick = bar(HealthBarLayout.RIGHT, thickness = 4f)
        assertEquals(4f, thick.background.right - thick.background.left, 0f)
        assertEquals(80f, thick.background.bottom - thick.background.top, 0f)
        assertNull(HealthBarLayout.calculate(50f, 20f, 10f, 100f, 0, 1f, 2f, 1f))
        assertNull(HealthBarLayout.calculate(Float.NaN, 20f, 50f, 100f, 0, 1f, 2f, 1f))
    }
}
