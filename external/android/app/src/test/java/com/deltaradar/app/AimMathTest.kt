package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class AimMathTest {
    @Test fun oldCentreLookPointMigratesToTheMeasuredRightHandArea() {
        assertEquals(.7f to .55f, AimMath.lookPointOrDefault(.49583334f, .49537036f))
        assertEquals(.8f to .4f, AimMath.lookPointOrDefault(.8f, .4f))
        assertFalse(AimMath.isLookPoint(.49583334f, .49537036f))
        assertFalse(AimMath.isLookPoint(Float.NaN, .5f))
    }

    private fun target(id: String = "enemy", x: Float = .5f, y: Float = .5f,
                       team: Int = 2, kind: String = "player", alive: Boolean = true,
                       onScreen: Boolean = true) =
        AimMath.Target(id, x, y, .02f, .04f, kind, team, alive, onScreen, positionPlaintext = true, renderedOnScreen = true)

    @Test fun rejectsUnknownEncryptedAndUnrenderedPositions() {
        assertNull(AimMath.select(listOf(target().copy(positionPlaintext = false)), 1920, 1080, 15))
        assertNull(AimMath.select(listOf(target().copy(renderedOnScreen = false)), 1920, 1080, 15))
        assertFalse(AimMath.crosshairOnBody(target().copy(renderedOnScreen = false)))
    }

    @Test fun usesACircleInPixelsOnWideScreens() {
        assertEquals(150f, AimMath.radius(2000, 1000, 15), .001f)
        val horizontal = target(x = .58f) // 160px from centre
        val vertical = target(y = .66f) // 160px from centre
        assertNull(AimMath.select(listOf(horizontal, vertical), 2000, 1000, 15))
        assertNotNull(AimMath.select(listOf(horizontal), 2000, 1000, 20))
    }

    @Test fun rejectsTeammatesUnknownTeamsLootDeadAndOffscreenTargets() {
        val excluded = listOf(target(team = 1), target(team = 0), target(kind = "loot"),
            target(kind = "vehicle"), target(alive = false), target(onScreen = false),
            target(x = Float.NaN))
        assertNull(AimMath.select(excluded, 1920, 1080, 45))
        assertNotNull(AimMath.select(listOf(target(kind = "ai")), 1920, 1080, 15))
    }

    @Test fun selectsClosestToCrosshairAndKeepsStableLock() {
        val old = target("old", x = .52f)
        val new = target("new", x = .519f)
        assertEquals("new", AimMath.select(listOf(old, new), 1920, 1080, 15)?.id)
        assertEquals("old", AimMath.select(listOf(old, new), 1920, 1080, 15, "old")?.id)
        assertEquals("new", AimMath.select(listOf(old.copy(alive = false), new),
            1920, 1080, 15, "old")?.id)
    }

    @Test fun diagonalOutsideCircleCannotBeSelected() {
        assertNull(AimMath.select(listOf(target(x = .6f, y = .6f)), 1000, 1000, 12))
    }

    @Test fun shootsOnlyWhenCrosshairIsInsideTorso() {
        assertTrue(AimMath.crosshairOnBody(target()))
        assertFalse(AimMath.crosshairOnBody(target(x = .53f)))
        assertFalse(AimMath.crosshairOnBody(target(y = .55f)))
    }

    @Test fun gestureCorrectionIsBoundedAndFollowsTargetDirection() {
        val step = AimMath.correction(target(x = .9f, y = .1f), 2000, 1000, 60)
        assertEquals(4, step.first)
        assertEquals(-4, step.second)
        assertEquals(0 to 0, AimMath.correction(target(), 2000, 1000, 60))
    }

    @Test fun smallErrorsDoNotTruncateToZeroOutsideANarrowBody() {
        val narrow = target(x = .5008f).copy(halfWidth = .0001f)
        assertFalse(AimMath.crosshairOnBody(narrow))
        assertEquals(1, AimMath.correction(narrow, 2000, 1000, 10).first)
    }

    @Test fun handlesRotationAndInvalidViewport() {
        assertEquals(AimMath.radius(1920, 1080, 15), AimMath.radius(1080, 1920, 15), .001f)
        assertNull(AimMath.select(listOf(target()), 0, 1000, 15))
        assertNull(AimMath.select(listOf(target()), 1000, 0, 15))
    }
}
