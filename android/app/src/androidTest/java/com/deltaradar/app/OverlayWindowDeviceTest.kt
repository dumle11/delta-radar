package com.deltaradar.app

import android.content.Intent
import android.graphics.Bitmap
import android.os.SystemClock
import android.provider.Settings as AndroidSettings
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import org.junit.After
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Real overlay windows driven by labelled test input; not evidence of live game positions. */
@RunWith(AndroidJUnit4::class)
class OverlayWindowDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private fun await(message: String, condition: () -> Boolean) {
        val until = SystemClock.elapsedRealtime() + 5000
        while (!condition() && SystemClock.elapsedRealtime() < until) SystemClock.sleep(20)
        assertTrue(message, condition())
    }
    private fun capture(name: String, markerVisible: Boolean) {
        instrumentation.waitForIdleSync()
        val deadline = SystemClock.elapsedRealtime() + 3000
        while (true) {
            val bitmap = instrumentation.uiAutomation.takeScreenshot()
            // The red fixture tracer occupies the top-centre strip. Verify the
            // composed screen, not merely View visibility or onDraw dispatch.
            var redPixels = 0
            for (x in bitmap.width / 2 - 4..bitmap.width / 2 + 4) {
                for (y in 50..150) {
                    val pixel = bitmap.getPixel(x, y)
                    if (android.graphics.Color.red(pixel) > 120 &&
                        android.graphics.Color.red(pixel) > android.graphics.Color.green(pixel) + 60 &&
                        android.graphics.Color.red(pixel) > android.graphics.Color.blue(pixel) + 60) redPixels++
                }
            }
            val matches = (redPixels > 40) == markerVisible
            if (matches || SystemClock.elapsedRealtime() >= deadline) {
                File(context.filesDir, name).outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
                bitmap.recycle()
                assertTrue("Composed screen marker visibility differs for $name", matches)
                return
            }
            bitmap.recycle()
            SystemClock.sleep(50)
        }
    }
    @After fun cleanup() {
        instrumentation.runOnMainSync { OverlayService.stop(context); EntityFeedService.stop(context) }
        await("overlay service did not stop") { OverlayService.instance == null }
    }
    @Test fun actualWindowsStayHiddenUntilShowAndClearOnFeedExpiry() {
        assumeTrue("Overlay permission required", AndroidSettings.canDrawOverlays(context))
        val settingsBefore = Settings.entityFeatures(context, Settings.EntityType.PLAYER)
        val modeBefore = Settings.playerDrawMode(context)
        val colorBefore = Settings.playerColor(context)
        val keepFeeding = AtomicBoolean(true)
        var feeder: Thread? = null
        try {
            Settings.setEntityFeatures(context, Settings.EntityType.PLAYER, Settings.ALL_FEATURES)
            Settings.setPlayerDrawMode(context, Settings.DRAW_MODE_BOXES)
            Settings.setPlayerColor(context, 0xFFFF3030.toInt())
            instrumentation.runOnMainSync { EntityFeedService.start(context); OverlayService.show(context) }
            await("services not ready") { EntityFeedService.instance?.isFeedRunning == true && OverlayService.instance != null }
            val service = EntityFeedService.instance!!
            val raw = """{"self":"TEST-camera","entities":[{"id":"TEST-camera","x":0,"y":0,"z":1.6,"yaw":0,"pitch":0,"fov":80},{"id":"TEST-enemy","name":"TEST FIXTURE","kind":"player","team":2,"x":10,"y":0,"z":1.0,"healthKnown":true,"health":75,"maxHealth":100}]}"""
            feeder = thread(isDaemon = true) {
                while (keepFeeding.get()) { service.ingest(raw); SystemClock.sleep(10) }
            }
            await("actual overlay did not draw") {
                OverlayService.instance?.overlayVisible == true && OverlayService.instance!!.overlayDrawnFrames > 0
            }
            capture("test-overlay-shown.png", true)
            instrumentation.runOnMainSync { OverlayService.hide(context) }
            await("Hide did not hide the window") { OverlayService.instance?.overlayVisible == false }
            SystemClock.sleep(350)
            assertFalse("pending work repainted hidden overlay", OverlayService.instance!!.overlayVisible)
            capture("test-overlay-hidden.png", false)
            val drawsBeforeShow = OverlayService.instance!!.overlayDrawnFrames
            instrumentation.runOnMainSync { OverlayService.show(context) }
            service.ingest(raw)
            await("Show did not resume actual rendering") {
                OverlayService.instance?.overlayVisible == true &&
                    OverlayService.instance!!.overlayDrawnFrames >= drawsBeforeShow + 2
            }
            capture("test-overlay-reshown.png", true)
            keepFeeding.set(false)
            feeder?.join(250)
            await("stale feed did not clear actual overlay") { OverlayService.instance?.overlayVisible == false }
            capture("test-overlay-stale.png", false)
        } finally {
            keepFeeding.set(false)
            feeder?.join(250)
            Settings.setEntityFeatures(context, Settings.EntityType.PLAYER, settingsBefore)
            Settings.setPlayerDrawMode(context, modeBefore)
            Settings.setPlayerColor(context, colorBefore)
        }
    }
}
