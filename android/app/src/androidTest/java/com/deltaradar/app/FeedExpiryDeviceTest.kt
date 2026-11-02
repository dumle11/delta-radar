package com.deltaradar.app

import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Same acceptance checks compile against both the original and repaired services. */
@RunWith(AndroidJUnit4::class)
class FeedExpiryDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private var client: LocalSocket? = null
    private fun ready(): EntityFeedService {
        instrumentation.runOnMainSync { EntityFeedService.start(context) }
        val deadline = SystemClock.elapsedRealtime() + 5000
        while (EntityFeedService.instance?.isFeedRunning != true && SystemClock.elapsedRealtime() < deadline)
            SystemClock.sleep(20)
        val service = EntityFeedService.instance!!
        val socket = LocalSocket()
        socket.connect(LocalSocketAddress("delta_radar_feed", LocalSocketAddress.Namespace.ABSTRACT))
        client = socket
        socket.outputStream.write(("""{"self":"test-camera","entities":[{"id":"test-camera","x":0,"y":0,"z":0,"yaw":0,"pitch":0,"fov":80},{"id":"test-enemy","x":10,"y":0,"z":0}]}""" + "\n").toByteArray())
        val receivedDeadline = SystemClock.elapsedRealtime() + 5000
        while (service.entityCount != 2 && SystemClock.elapsedRealtime() < receivedDeadline) SystemClock.sleep(20)
        assertEquals(2, service.entityCount)
        return service
    }
    @After fun cleanup() {
        client?.close()
        instrumentation.runOnMainSync { EntityFeedService.stop(context) }
        val deadline = SystemClock.elapsedRealtime() + 5000
        while (EntityFeedService.instance != null && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(20)
        assertNull(EntityFeedService.instance)
    }
    @Test fun silentSocketDoesNotRetainVisibleEntities() {
        val service = ready()
        SystemClock.sleep(1000)
        assertEquals("stalled feed retained entity state", 0, service.entityCount)
    }
    @Test fun oneEmptySnapshotClearsThePreviousMatch() {
        val service = ready()
        client!!.outputStream.write("{\"self\":\"\",\"entities\":[]}\n".toByteArray())
        SystemClock.sleep(100)
        assertEquals("empty loading frame retained previous actors", 0, service.entityCount)
    }
}
