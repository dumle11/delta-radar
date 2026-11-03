package com.deltaradar.app

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ProjectionMathTest {
    @Test fun narrowScopeUsesItsActualFieldOfView() {
        val p = ProjectionMath.project(100.0, 5.0, 0.0, 0.0, 0.0, 20.0, 16.0 / 9.0)
        assertTrue(p.onScreen)
        assertEquals(0.64178f, p.x, 0.0001f)
    }

    @Test fun yAxisIsForwardAtZeroYaw() {
        // Y=100 forward, X=0 altitude, Z=0 lateral → center of screen
        val p = ProjectionMath.project(100.0, 0.0, 0.0, 0.0, 0.0, 80.0, 16.0 / 9.0)
        assertTrue(p.onScreen)
        assertEquals(0.5f, p.x, 0.001f)
        assertEquals(0.5f, p.y, 0.001f)
    }

    @Test fun zAxisMovesRight() {
        // Z=25 lateral, Y=100 forward → right of center
        val p = ProjectionMath.project(100.0, 25.0, 0.0, 0.0, 0.0, 80.0, 16.0 / 9.0)
        assertTrue(p.onScreen)
        assertTrue(p.x > 0.5f)
        assertEquals(0.5f, p.y, 0.001f)
    }

    @Test fun xAxisMovesUp() {
        // X=10 altitude (above), Y=100 forward → above center on screen
        val p = ProjectionMath.project(100.0, 0.0, 10.0, 0.0, 0.0, 80.0, 16.0 / 9.0)
        assertTrue(p.onScreen)
        assertTrue(p.y < 0.5f)
    }

    @Test fun lookingUpMovesAWorldAnchoredTargetDownTheScreen() {
        val level = ProjectionMath.project(100.0, 0.0, 0.0, 0.0, 0.0, 80.0, 16.0 / 9.0)
        val lookingUp = ProjectionMath.project(100.0, 0.0, 0.0, 0.0, 12.0, 80.0, 16.0 / 9.0)
        assertTrue(lookingUp.y > level.y)
    }

    @Test fun behindTargetNotInFront() {
        // Y=-100 behind camera
        val behind = ProjectionMath.project(-100.0, 0.0, 0.0, 0.0, 0.0, 80.0, 16.0 / 9.0)
        assertFalse(behind.inFront)
        assertFalse(behind.onScreen)
        assertEquals(0.982f, behind.y, 0.001f)
        assertEquals(0.5f, behind.x, 0.001f)
    }

    @Test fun behindTargetsPreserveSideAlongBottomEdge() {
        val behindLeft = ProjectionMath.project(-100.0, -50.0, 0.0, 0.0, 0.0, 80.0, 16.0 / 9.0)
        val behindRight = ProjectionMath.project(-100.0, 50.0, 0.0, 0.0, 0.0, 80.0, 16.0 / 9.0)
        assertTrue(behindLeft.x < 0.5f)
        assertTrue(behindRight.x > 0.5f)
        assertEquals(0.982f, behindLeft.y, 0.001f)
        assertEquals(0.982f, behindRight.y, 0.001f)
    }

    @Test fun offscreenClampsToEdge() {
        // Z=100 far right, Y=10 barely forward → off-screen right, clamped
        val side = ProjectionMath.project(10.0, 100.0, 0.0, 0.0, 0.0, 80.0, 16.0 / 9.0)
        assertFalse(side.onScreen)
        assertTrue(side.x in 0.018f..0.982f)
        assertTrue(side.y in 0.018f..0.982f)
    }
}
