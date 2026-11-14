package com.deltaradar.app

import org.junit.Assert.assertEquals
import org.junit.Test

class AimFeedbackTest {
    private val pending = AimFeedback(10.0, 30.0, 0.0, 90.0, 100f)
    @Test fun movingEnemyDoesNotInvalidateCorrectCameraResponse() {
        val t = AimMath.Target("enemy", .6f, .5f, .02f, .04f, "player", 2,
            positionPlaintext = true, renderedOnScreen = true)
        val f = AimFeedback.capture(10.0, 0.0, 0.0, 90.0, t, 2000, 1000)
        // The actor moved away, but the camera turned toward its old bearing.
        assertEquals(AimFeedback.Result.IMPROVED, f.observe(10.1, 1.0, 0.0, 90.0, 350f, 100))
        assertEquals(AimFeedback.Result.STOP, f.observe(10.1, -1.0, 0.0, 90.0, 100f, 100))
    }

    @Test fun noFurtherSwipeUntilCameraAdvances() {
        assertEquals(AimFeedback.Result.WAIT, pending.observe(10.0, 30.0, 0.0, 90.0, 100f, 100))
        assertEquals(AimFeedback.Result.STOP, pending.observe(10.0, 30.0, 0.0, 90.0, 100f, 1400))
    }
    @Test fun wrongDirectionAndNoResponseStopRatherThanSpin() {
        assertEquals(AimFeedback.Result.STOP, pending.observe(10.1, 29.5, 0.0, 90.0, 105f, 100))
        assertEquals(AimFeedback.Result.STOP, pending.observe(10.1, 30.0, 0.0, 90.0, 100f, 100))
        assertEquals(AimFeedback.Result.STOP, pending.observe(10.1, 30.0, 0.0, 90.0, 80f, 100))
    }
    @Test fun advancingCameraAndImprovingErrorAllowAnotherCorrection() {
        assertEquals(AimFeedback.Result.IMPROVED, pending.observe(10.1, 30.5, .1, 90.0, 95f, 100))
    }
    @Test fun cameraJumpZoomChangeAndWorldTimeResetYieldToThePlayer() {
        assertEquals(AimFeedback.Result.YIELD, pending.observe(10.1, 120.0, 0.0, 90.0, 50f, 100))
        assertEquals(AimFeedback.Result.YIELD, pending.observe(10.1, 30.5, 0.0, 45.0, 50f, 100))
        assertEquals(AimFeedback.Result.YIELD, pending.observe(1.0, 30.5, 0.0, 90.0, 50f, 100))
    }
    @Test fun wrappingYawDoesNotLookLikeAFullTurn() {
        val wrapped = AimFeedback(10.0, 179.9, 0.0, 90.0, 100f)
        assertEquals(AimFeedback.Result.IMPROVED, wrapped.observe(10.1, -179.9, 0.0, 90.0, 95f, 100))
    }
}
