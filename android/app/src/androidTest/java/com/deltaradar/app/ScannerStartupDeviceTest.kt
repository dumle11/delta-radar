package com.deltaradar.app

import android.app.Service
import android.content.Intent
import android.os.SystemClock
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** Real service/activity lifecycles with mock root commands; no game reads/input. */
@RunWith(AndroidJUnit4::class)
class ScannerStartupDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext

    private class Commands {
        val launches = AtomicInteger()
        val calls = AtomicInteger()
        val gameReady = AtomicBoolean(true)
        val health = AtomicReference("RUNNING")
        val verificationFails = AtomicBoolean(false)
        val earlyExits = AtomicInteger()
        val launchGameVanishes = AtomicBoolean(false)
        val alwaysExit = AtomicBoolean(false)
        var entered: CountDownLatch? = null
        var release: CountDownLatch? = null

        fun execute(command: String, cancellation: CommandRunner.Cancellation): CommandRunner.Result {
            calls.incrementAndGet()
            if (command.contains("--identity")) {
                entered?.countDown()
                release?.await(5, TimeUnit.SECONDS)
                if (verificationFails.get()) return CommandRunner.Result("", null, true, false)
                val identity = JSONObject().put("project", "delta-radar").put("api", 31)
                    .put("sourceSha256", BuildConfig.SCANNER_SOURCESHA256).put("revision", BuildConfig.SCANNER_REVISION)
                return ok("uid=0(root)\n$identity")
            }
            if (cancellation.isCancelled()) return CommandRunner.Result("", null, false, true)
            return when {
                command.contains("game=\$(pidof") -> if (gameReady.get()) ok("12345")
                    else CommandRunner.Result("GAME_NOT_RUNNING", 3, false, false)
                command.contains("nohup") -> if (launchGameVanishes.compareAndSet(true, false)) {
                    gameReady.set(false)
                    CommandRunner.Result("GAME_NOT_RUNNING", 3, false, false)
                } else ok((42000 + launches.incrementAndGet()).toString())
                command.contains("echo SCANNER_READY") -> ok(if (earlyExits.getAndUpdate { maxOf(0, it - 1) } > 0) "EXITED" else "SCANNER_READY")
                command.contains("echo EXITED") -> if (health.get() == "TIMEOUT") CommandRunner.Result("", null, true, false)
                    else ok(if (alwaysExit.get()) "EXITED" else health.getAndSet("RUNNING"))
                else -> error("Unexpected controlled command: $command")
            }
        }

        private fun ok(output: String) = CommandRunner.Result(output, 0, false, false)
    }

    private fun await(message: String, timeoutMs: Long = 8_000, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (!condition() && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(20)
        assertTrue("$message; state=${ScannerRuntime.state}", condition())
    }

    @Before fun before() = stop()
    @After fun after() = stop()

    private fun stop() {
        instrumentation.runOnMainSync { EntityFeedService.stop(context); OverlayService.stop(context) }
        await("service did not stop") { EntityFeedService.instance == null && ScannerRuntime.state.phase == ScannerRuntime.Phase.IDLE }
    }

    private fun listener(commands: Commands): EntityFeedService {
        instrumentation.runOnMainSync { EntityFeedService.start(context) }
        await("listener did not start") { EntityFeedService.instance?.isFeedRunning == true }
        return EntityFeedService.instance!!.also { service ->
            instrumentation.runOnMainSync { service.scannerExecutor = commands::execute }
        }
    }

    private fun request(service: EntityFeedService) {
        instrumentation.runOnMainSync {
            service.onStartCommand(Intent(context, EntityFeedService::class.java)
                .setAction(EntityFeedService.ACTION_START_SCANNER), 0, 1)
        }
    }

    @Test fun waitsForGameThenLaunchesOnceAndDuplicateStartIsIdempotent() {
        val commands = Commands().apply { gameReady.set(false) }
        val service = listener(commands)
        request(service)
        await("did not wait for game") { ScannerRuntime.state.phase == ScannerRuntime.Phase.WAITING_FOR_GAME }
        request(service)
        assertEquals(0, commands.launches.get())
        commands.gameReady.set(true)
        await("did not start after game opened") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
        request(service)
        SystemClock.sleep(200)
        assertEquals(1, commands.launches.get())
    }

    @Test fun stopWhileWaitingPreventsLaterLaunch() {
        val commands = Commands().apply { gameReady.set(false) }
        val service = listener(commands)
        request(service)
        await("did not wait for game") { ScannerRuntime.state.phase == ScannerRuntime.Phase.WAITING_FOR_GAME }
        stop()
        commands.gameReady.set(true)
        SystemClock.sleep(300)
        assertEquals(0, commands.launches.get())
    }

    @Test fun gameDisappearingBeforeDispatchWaitsWithoutLaunchingTwice() {
        val commands = Commands().apply { launchGameVanishes.set(true) }
        val service = listener(commands)
        request(service)
        await("did not return to game waiting") { ScannerRuntime.state.phase == ScannerRuntime.Phase.WAITING_FOR_GAME }
        assertEquals(0, commands.launches.get())
        commands.gameReady.set(true)
        await("did not start after game returned") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
        assertEquals(1, commands.launches.get())
    }

    @Test fun earlyExitDuringReadinessRecoversWithANewLease() {
        val commands = Commands().apply { earlyExits.set(2) }
        request(listener(commands))
        await("early child exit was not recovered") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
        assertEquals(3, commands.launches.get())
    }

    @Test fun destroyingServiceDuringVerificationCancelsPendingLaunch() {
        val commands = Commands().apply { entered = CountDownLatch(1); release = CountDownLatch(1) }
        val service = listener(commands)
        request(service)
        try {
            assertTrue(commands.entered!!.await(2, TimeUnit.SECONDS))
            instrumentation.runOnMainSync { EntityFeedService.stop(context) }
            await("service did not destroy") { EntityFeedService.instance == null }
        } finally { commands.release!!.countDown() }
        await("pending start was not cancelled") { ScannerRuntime.state.phase == ScannerRuntime.Phase.IDLE }
        assertEquals(0, commands.launches.get())
    }

    @Test fun screenRecreationDoesNotCancelServiceOwnedStartup() {
        val commands = Commands().apply { entered = CountDownLatch(1); release = CountDownLatch(1) }
        val service = listener(commands)
        val activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        val monitor = instrumentation.addMonitor(MainActivity::class.java.name, null, false)
        var recreated: android.app.Activity? = null
        try {
            request(service)
            assertTrue(commands.entered!!.await(2, TimeUnit.SECONDS))
            instrumentation.runOnMainSync { activity.recreate() }
            recreated = monitor.waitForActivityWithTimeout(3_000)
            assertNotNull("screen did not recreate", recreated)
            assertNotSame(activity, recreated)
            assertTrue(activity.isDestroyed)
            commands.release!!.countDown()
            await("screen recreation cancelled startup") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
            assertSame(service, EntityFeedService.instance)
            assertEquals(1, commands.launches.get())
        } finally {
            commands.release!!.countDown()
            instrumentation.removeMonitor(monitor)
            instrumentation.runOnMainSync { recreated?.finish(); if (!activity.isDestroyed) activity.finish() }
        }
    }

    @Test fun stickyRecreationRestoresRequestedScanner() {
        val commands = Commands()
        val old = listener(commands)
        request(old)
        await("first scanner did not start") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
        // Emulate system service recreation, retaining the durable Start request.
        instrumentation.runOnMainSync { context.stopService(Intent(context, EntityFeedService::class.java)) }
        await("old service did not stop") { EntityFeedService.instance == null && ScannerRuntime.state.phase == ScannerRuntime.Phase.IDLE }
        val next = listener(commands)
        instrumentation.runOnMainSync { assertEquals(Service.START_STICKY, next.onStartCommand(null, 0, 2)) }
        await("restored service omitted scanner") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
        assertNotSame(old, next)
        assertEquals(2, commands.launches.get())
    }

    @Test fun listenerOnlyAndStoppedRequestsNeverRestoreScanner() {
        val commands = Commands()
        val service = listener(commands)
        instrumentation.runOnMainSync { assertEquals(Service.START_NOT_STICKY, service.onStartCommand(null, 0, 1)) }
        assertEquals(0, commands.calls.get())
        request(service)
        await("scanner did not start") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
        stop()
        val next = listener(commands)
        instrumentation.runOnMainSync { assertEquals(Service.START_NOT_STICKY, next.onStartCommand(null, 0, 2)) }
        SystemClock.sleep(200)
        assertEquals(1, commands.launches.get())
    }

    @Test fun failedVerificationCanRetryWithListenerStillRunning() {
        val commands = Commands().apply { verificationFails.set(true) }
        val service = listener(commands)
        request(service)
        await("uncertain verification did not fail") { ScannerRuntime.state.phase == ScannerRuntime.Phase.FAILED }
        assertEquals(0, commands.launches.get())
        assertTrue(service.isFeedRunning)
        val activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        try {
            val label = AtomicReference("")
            await("failed listener showed Stop instead of Retry") {
                instrumentation.runOnMainSync { label.set(activity.findViewById<TextView>(R.id.btnStart).text.toString()) }
                label.get() == "Retry scanner"
            }
            commands.verificationFails.set(false)
            instrumentation.runOnMainSync { activity.findViewById<TextView>(R.id.btnStart).performClick() }
            await("retry button did not start") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
            assertEquals(1, commands.launches.get())
        } finally { instrumentation.runOnMainSync { activity.finish() } }
    }

    @Test fun confirmedExitRecoversButUncertainHealthCheckDoesNotRelaunch() {
        val commands = Commands()
        val service = listener(commands)
        request(service)
        await("scanner did not start") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
        commands.health.set("EXITED")
        await("confirmed exit was not recovered") { commands.launches.get() == 2 && ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
        commands.health.set("TIMEOUT")
        await("uncertain process state was not surfaced") { ScannerRuntime.state.phase == ScannerRuntime.Phase.FAILED }
        SystemClock.sleep(200)
        assertEquals(2, commands.launches.get())
    }

    @Test fun repeatedConfirmedExitsStopAtRecoveryLimit() {
        val commands = Commands().apply { alwaysExit.set(true) }
        request(listener(commands))
        await("crash loop did not stop", 20_000) { ScannerRuntime.state.phase == ScannerRuntime.Phase.FAILED }
        assertEquals(4, commands.launches.get())
        assertTrue(ScannerRuntime.state.message.contains("keeps exiting"))
    }
}
