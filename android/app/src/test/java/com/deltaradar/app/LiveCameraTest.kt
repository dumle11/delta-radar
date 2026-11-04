package com.deltaradar.app
import org.junit.Assert.*
import org.junit.Test
class LiveCameraTest {
    private val camera=LiveCamera(20,"0x123",1000,1.0,true,1.0,2.0,3.0,90.0,0.0,0.0,90.0)
    @Test fun rejectsOldCameraAndWrongProcessOrWorld() {
        assertTrue(camera.usable(20,"0x123",1100))
        assertFalse(camera.usable(20,"0x123",1101))
        assertFalse(camera.usable(21,"0x123",1001))
        assertFalse(camera.usable(20,"0x124",1001))
        assertFalse(camera.usable(20,"0x123",999))
        assertFalse(camera.copy(valid=false).usable(20,"0x123",1001))
        assertFalse(camera.copy(roll=Double.NaN).usable(20,"0x123",1001))
    }
}
