package com.deltaradar.app

import android.content.Intent
import android.net.LocalSocket
import android.net.LocalSocketAddress
import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.After
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Exercises Android's real socket/service path. Never launches the game or scanner. */
@RunWith(AndroidJUnit4::class)
class FeedLifecycleDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val clients = mutableListOf<LocalSocket>()
    private val frame = """{"self":"test-camera","entities":[{"id":"test-camera","x":0,"y":0,"z":0,"yaw":0,"pitch":0,"fov":80},{"id":"test-enemy","x":10,"y":0,"z":0}]}"""

    private fun await(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 5000
        while (!condition() && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(20)
        assertTrue(message, condition())
    }
    private fun start(): EntityFeedService {
        instrumentation.runOnMainSync { EntityFeedService.start(context) }
        await("listener did not start") { EntityFeedService.instance?.isFeedRunning == true }
        return EntityFeedService.instance!!
    }
    private fun connect() = LocalSocket().also {
        it.connect(LocalSocketAddress("x7f3a91", LocalSocketAddress.Namespace.ABSTRACT))
        clients.add(it)
    }
    private fun send(client: LocalSocket, line: String) {
        client.outputStream.write((line + "\n").toByteArray())
        client.outputStream.flush()
    }
    @After fun cleanup() {
        clients.forEach { try { it.close() } catch (_: Exception) {} }
        instrumentation.runOnMainSync { EntityFeedService.stop(context) }
        await("service did not stop") { EntityFeedService.instance == null }
    }
    @Test fun validFeedStallsClearsAndReconnects() {
        val service = start()
        assertFalse(service.feedStatus.hasCamera)
        val client = connect()
        send(client, frame)
        await("valid frame not received") { service.feedStatus.hasCamera }
        assertEquals(2, service.entityCount)
        // Leave the real socket connected but silent: freshness must expire.
        await("silent client kept stale entities alive") { !service.feedStatus.fresh }
        assertEquals(0, service.entityCount)
        client.close()
        await("disconnect not cleared") { service.feedStatus.receivedAtMs < 0 }
        val next = connect()
        send(next, frame)
        await("reconnect did not receive a fresh frame") { service.feedStatus.hasCamera }
        send(next, """{"self":"","entities":[]}""")
        await("empty frame did not clear match") { service.feedStatus.fresh && !service.feedStatus.hasCamera }
        assertEquals(0, service.entityCount)
    }
    @Test fun stopWhileReadIsBlockedReleasesSocketForNextService() {
        val old = start()
        val client = connect()
        send(client, frame)
        await("valid frame not received") { old.feedStatus.hasCamera }
        instrumentation.runOnMainSync { EntityFeedService.stop(context) }
        await("old service did not stop") { EntityFeedService.instance == null }
        assertFalse(old.feedStatus.fresh)
        val next = start()
        assertNotSame(old, next)
        val reconnected = connect()
        send(reconnected, frame)
        await("restarted service cannot accept") { next.feedStatus.hasCamera }
        assertFalse(old.feedStatus.fresh)
    }
    @Test fun stopWhileAcceptIsBlockedReleasesSocketForNextService() {
        val old = start()
        instrumentation.runOnMainSync { EntityFeedService.stop(context) }
        await("idle service did not stop") { EntityFeedService.instance == null }
        val next = start()
        assertNotSame(old, next)
        val client = connect()
        send(client, frame)
        await("old acceptor retained the socket address") { next.feedStatus.hasCamera }
    }

}
