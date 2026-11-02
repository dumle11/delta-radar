package com.deltaradar.app

import android.os.SystemClock
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import java.util.concurrent.atomic.AtomicInteger
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Delays the verification result, then cancels before a launch can dispatch. */
@RunWith(AndroidJUnit4::class)
class ScannerCancellationDeviceTest {
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private fun cancelledVerification(useActivityDestructionPath: Boolean) {
        val entered = CountDownLatch(1)
        val release = CountDownLatch(1)
        val launchCalls = AtomicInteger()
        val session = ScannerRuntime.start(context) { command, _ ->
            if (command.contains("nohup")) launchCalls.incrementAndGet()
            entered.countDown()
            release.await(3, TimeUnit.SECONDS)
            val identity = JSONObject().put("project", "delta-radar").put("api", 31)
                .put("sourceSha256", BuildConfig.SCANNER_SOURCESHA256).put("revision", BuildConfig.SCANNER_REVISION)
            CommandRunner.Result("uid=0(root)\n$identity", 0, false, false)
        }
        try {
            assertTrue(entered.await(2, TimeUnit.SECONDS))
            if (useActivityDestructionPath) ScannerRuntime.cancelPending(session) else ScannerRuntime.stop()
            assertFalse(session.lease.exists())
            release.countDown()
            val deadline = SystemClock.elapsedRealtime() + 5000
            while (ScannerRuntime.state.phase == ScannerRuntime.Phase.STOPPING && SystemClock.elapsedRealtime() < deadline)
                SystemClock.sleep(20)
            assertEquals(ScannerRuntime.Phase.IDLE, ScannerRuntime.state.phase)
            assertEquals("cancelled delayed worker dispatched a launch", 0, launchCalls.get())
        } finally { release.countDown(); ScannerRuntime.stop() }
    }
    @Test fun stopBeforeVerificationReturnsNeverDispatchesLaunch() = cancelledVerification(false)
    @Test fun activityDestructionCancellationRejectsLateVerification() = cancelledVerification(true)
}
