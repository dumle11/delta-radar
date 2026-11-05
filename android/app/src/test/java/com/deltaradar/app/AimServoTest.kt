package com.deltaradar.app
import org.junit.Assert.*
import org.junit.Test
import kotlin.math.abs

class AimServoTest {
    private fun target(x: Float, y: Float = .5f) = AimMath.Target("bot",x,y,.03f,.05f,"ai",2,
        positionPlaintext=true,renderedOnScreen=true)
    @Test fun calibratesAndConvergesInAFewFreshCameraFrames() {
        val servo=AimServo();var yaw=0.0; var pitch=0.0
        repeat(6) { i ->
            val p=ProjectionMath.project(100.0,17.6327,0.0,yaw,pitch,90.0,2.0,clipToEdge=false)
            val step=servo.step(target(p.x,p.y),2000,1000,1+i/30.0,yaw,pitch,90.0,100,i*33L)
            yaw+=step.first*.1; pitch-=step.second*.08
        }
        assertTrue("yaw=$yaw",abs(yaw-10)<.15)
        assertNull(servo.fault)
        assertTrue(abs(pitch) < .15)
    }
    @Test fun sendsOnlyOncePerCameraPoseAndContinuesInsideTheBody() {
        val servo=AimServo()
        val first=servo.step(target(.51f),2000,1000,1.0,0.0,0.0,90.0,100,0)
        assertTrue(first.first>0)
        assertEquals(0f to 0f,servo.step(target(.52f),2000,1000,1.0,0.0,0.0,90.0,100,16))
    }
    @Test fun oppositeCameraResponseStopsAndZoomRestartsCalibration() {
        val servo=AimServo()
        servo.step(target(.6f),2000,1000,1.0,0.0,0.0,90.0,100,0)
        servo.step(target(.61f),2000,1000,1.1,-1.0,0.0,90.0,100,100)
        assertNotNull(servo.fault)
        servo.reset()
        assertEquals(0f to 0f,servo.step(target(.5f),2000,1000,2.0,0.0,0.0,45.0,100,200,calibrate=false))
        assertNull(servo.fault)
    }
    @Test fun anyCombinationAndNoTargetTypesAreSupported() {
        val bot=target(.5f);val enemy=bot.copy(id="enemy",kind="player");val friend=enemy.copy(id="friend",team=1)
        val all=listOf(bot,enemy,friend)
        assertNull(AimMath.select(all,2000,1000,20,types=0))
        assertEquals("friend",AimMath.select(all,2000,1000,20,types=AimMath.FRIENDLIES)?.id)
        assertEquals("enemy",AimMath.select(all,2000,1000,20,types=AimMath.ENEMIES)?.id)
        assertEquals("bot",AimMath.select(all,2000,1000,20,types=AimMath.BOTS)?.id)
        assertNotNull(AimMath.select(all,2000,1000,20,types=AimMath.ALL_TYPES))
    }
    @Test fun acquisitionCircleDoesNotStealALiveLockWhileMoving() {
        val old=target(.8f);val nearer=target(.501f).copy(id="other")
        assertEquals("other",AimMath.select(listOf(old,nearer),2000,1000,15)?.id)
        assertEquals("bot",AimMath.select(listOf(old,nearer),2000,1000,15,"bot")?.id)
        assertEquals("other",AimMath.select(listOf(old.copy(alive=false),nearer),2000,1000,15,"bot")?.id)
    }
    @Test fun delayedCameraFeedbackDoesNotRepeatTheFullCorrection() {
        val servo = AimServo(.04); var yaw = 0.0
        val pending = java.util.ArrayDeque<Float>(); pending.add(0f); pending.add(0f)
        var peak = 0.0
        repeat(16) { i ->
            yaw += pending.removeFirst() * .04
            val p = ProjectionMath.project(100.0, 17.6327, 0.0, yaw, 0.0, 90.0, 2.0, clipToEdge = false)
            val step = servo.step(target(p.x), 2000, 1000, 1 + i / 30.0, yaw, 0.0, 90.0, 100, i * 33L, calibrate = false)
            pending.addLast(step.first); peak = maxOf(peak, yaw)
        }
        assertTrue("yaw=$yaw", abs(yaw - 10) < .1)
        assertTrue("overshoot=$peak", peak < 10.2)
    }
    @Test fun fullResponseExceedsTheOldSixtyPercentLimit() {
        val fast = AimServo().step(target(.53f),2000,1000,1.0,0.0,0.0,90.0,100,0,calibrate=false)
        val gentle = AimServo().step(target(.53f),2000,1000,1.0,0.0,0.0,90.0,60,0,calibrate=false)
        assertTrue(fast.first > gentle.first)
    }
    @Test fun trackingResetKeepsMeasuredSensitivityAndOlderPosesAreIgnored() {
        val servo = AimServo(.2)
        servo.step(target(.6f),2000,1000,1.0,0.0,0.0,90.0,100,0)
        servo.step(target(.59f,.51f),2000,1000,1.1,1.6,-1.6,90.0,100,110)
        servo.reset()
        val step = servo.step(target(.53f),2000,1000,2.0,0.0,0.0,90.0,100,200,calibrate=false)
        assertTrue("step=$step", step.first * .2 < 5)
        assertEquals(0f to 0f,servo.step(target(.7f),2000,1000,1.9,0.0,0.0,90.0,100,220))
    }
    @Test fun physicalInputCancelsAnInFlightProbeBeforeLearningOrFaulting() {
        val servo = AimServo()
        servo.step(target(.6f),2000,1000,1.0,0.0,0.0,90.0,100,0)
        servo.step(target(.61f),2000,1000,1.1,-2.0,3.0,90.0,100,110,calibrate=false)
        assertNull(servo.fault)
        assertTrue(servo.diagnostics().endsWith("probe=false"))
        assertTrue(servo.diagnostics().contains("sensitivity=0.2,0.2"))
        val pulse = servo.step(target(.6f),2000,1000,1.2,-2.0,3.0,90.0,100,600)
        assertEquals(8f,pulse.first)
        assertTrue(servo.diagnostics().endsWith("probe=true"))
    }
    @Test fun zoomTransitionCancelsCalibrationAndWaitsForStableFov() {
        val servo = AimServo()
        servo.step(target(.6f),2000,1000,1.0,0.0,0.0,90.0,100,0)
        servo.step(target(.5f),2000,1000,1.1,-2.0,3.0,70.0,100,110)
        assertNull(servo.fault)
        assertTrue(servo.diagnostics().endsWith("probe=false"))
        servo.step(target(.5f),2000,1000,1.2,-2.0,3.0,70.0,100,150)
        assertTrue(servo.diagnostics().endsWith("probe=false"))
        servo.step(target(.5f),2000,1000,1.3,-2.0,3.0,70.0,100,220)
        assertTrue(servo.diagnostics().endsWith("probe=true"))
    }
    @Test fun calibrationWaitsForEarlierCorrectionsToFinish() {
        val servo = AimServo()
        val correction = servo.step(target(.6f),2000,1000,1.0,0.0,0.0,90.0,100,0,calibrate=false)
        assertTrue(correction.first>8)
        assertEquals(0f to 0f,servo.step(target(.6f),2000,1000,1.05,0.0,0.0,90.0,100,50))
        assertTrue(servo.diagnostics().endsWith("probe=false"))
        val yaw = correction.first * .2
        servo.step(target(.55f),2000,1000,1.1,yaw,0.0,90.0,100,110)
        assertTrue(servo.diagnostics().endsWith("probe=true"))
    }
}
