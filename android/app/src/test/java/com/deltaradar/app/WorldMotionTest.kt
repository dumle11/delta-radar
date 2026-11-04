package com.deltaradar.app
import org.junit.Assert.*
import org.junit.Test
class WorldMotionTest {
    @Test fun runningCompensationUsesSampleAgeAndIsBounded() {
        val v=WorldMotion.velocity(1.0,0.0,0.0,1100,0.0,0.0,0.0,1000)
        assertEquals(10.0,v.x,0.0001)
        assertEquals(.2,WorldMotion.offset(v,1100,1120).x,0.0001)
        assertEquals(.75,WorldMotion.offset(v,1100,1200).x,0.0001)
        assertEquals(0.0,WorldMotion.offset(v,1100,1300).x,0.0001)
    }
    @Test fun teleportsAndInvalidTimesCannotCreateGhostMotion() {
        assertEquals(WorldMotion.Velocity(),WorldMotion.velocity(100.0,0.0,0.0,1100,0.0,0.0,0.0,1000))
        assertEquals(WorldMotion.Velocity(),WorldMotion.velocity(1.0,0.0,0.0,1000,0.0,0.0,0.0,1000))
        assertEquals(WorldMotion.Velocity(),WorldMotion.offset(WorldMotion.Velocity(4.0),1100,1000))
    }
}
