package com.deltaradar.app

import android.content.Intent
import android.os.SystemClock
import android.widget.TextView
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Opt-in real scanner check. Run only during a user-approved break. */
@RunWith(AndroidJUnit4::class)
class ScannerStartupLiveDeviceTest {
    @Test fun actualStartButtonLaunchesOnePinnedScannerAcrossScreenRecreation() {
        assumeTrue(InstrumentationRegistry.getArguments().getString("liveScanner") == "true")
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        fun await(message: String, condition: () -> Boolean) {
            val deadline = SystemClock.elapsedRealtime() + 15_000
            while (!condition() && SystemClock.elapsedRealtime() < deadline) SystemClock.sleep(50)
            assertTrue("$message; state=${ScannerRuntime.state}", condition())
        }
        instrumentation.runOnMainSync { EntityFeedService.stop(context); OverlayService.stop(context) }
        await("old service did not stop") { EntityFeedService.instance == null && ScannerRuntime.state.phase == ScannerRuntime.Phase.IDLE }
        var activity = instrumentation.startActivitySync(Intent(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
        try {
            repeat(2) { cycle ->
                instrumentation.runOnMainSync { activity.findViewById<TextView>(R.id.btnStart).performClick() }
                if (cycle == 0) {
                    // Reproduce rotating/leaving the screen during verification.
                    val monitor = instrumentation.addMonitor(MainActivity::class.java.name, null, false)
                    try {
                        instrumentation.runOnMainSync { activity.recreate() }
                        activity = checkNotNull(monitor.waitForActivityWithTimeout(5_000))
                    } finally { instrumentation.removeMonitor(monitor) }
                }
                await("Start button omitted scanner") { ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING }
                val pid = ScannerRuntime.state.message.substringAfter("Scanner PID ").substringBefore(';').toInt()
                val identity = RootUtils.run("readlink /proc/$pid/exe")
                assertTrue(identity.success)
                assertEquals(BuildConfig.SCANNER_PATH, identity.output)
                // Wait beyond the watchdog interval and confirm it did not
                // mistake a live owned scanner for a missing process.
                SystemClock.sleep(3_500)
                assertEquals(ScannerRuntime.Phase.RUNNING, ScannerRuntime.state.phase)
                assertTrue(ScannerRuntime.state.message.contains("PID $pid;"))
                instrumentation.runOnMainSync { activity.findViewById<TextView>(R.id.btnStart).performClick() }
                await("Stop button left scanner session active") { ScannerRuntime.state.phase == ScannerRuntime.Phase.IDLE && EntityFeedService.instance == null }
                assertFalse(RootUtils.run("[ \"\$(readlink /proc/$pid/exe)\" = ${ScannerRuntime.quote(BuildConfig.SCANNER_PATH)} ]").success)
            }
        } finally {
            instrumentation.runOnMainSync { EntityFeedService.stop(context); OverlayService.stop(context); activity.finish() }
        }
    }
}
