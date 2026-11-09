package com.deltaradar.app

import android.os.SystemClock
import android.provider.Settings as AndroidSettings
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Labelled synthetic values exercise the installed service/projection path. */
@RunWith(AndroidJUnit4::class)
class LootValueDeviceTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private fun await(message: String, condition: () -> Boolean) {
        val until = SystemClock.elapsedRealtime() + 6000
        while (!condition() && SystemClock.elapsedRealtime() < until) SystemClock.sleep(30)
        assertTrue(message, condition())
    }
    @Test fun individualValuesFilterThroughActualOverlayService() {
        assumeTrue(AndroidSettings.canDrawOverlays(context))
        val guide = Settings.containerGuide(context)
        val features = Settings.entityFeatures(context, Settings.EntityType.LOOT)
        val filter = Settings.lootValueFilter(context)
        val minimum = Settings.lootMinimumValue(context)
        val mode = Settings.lootDrawMode(context)
        val capture = File(context.filesDir, "capture-feed.enabled")
        val captureBefore = capture.isFile
        val output = File(context.filesDir, "latest-overlay-frame.json")
        val keepFeeding = AtomicBoolean(true)
        var feeder: Thread? = null
        try {
            capture.writeText("")
            Settings.setContainerGuide(context, false)
            Settings.setEntityFeatures(context, Settings.EntityType.LOOT, Settings.ALL_FEATURES)
            Settings.setLootDrawMode(context, Settings.DRAW_MODE_BOXES)
            Settings.setLootValueFilter(context, true)
            Settings.setLootMinimumValue(context, 20_000)
            instrumentation.runOnMainSync { EntityFeedService.start(context); OverlayService.show(context) }
            await("services not ready") { EntityFeedService.instance?.isFeedRunning == true && OverlayService.instance != null }
            val raw = """{"self":"TEST-value-camera","entities":[
                {"id":"TEST-value-camera","x":0,"y":0,"z":1.6,"yaw":0,"pitch":0,"fov":80},
                {"id":"TEST-value-cheap-stack","name":"TEST CHEAP STACK","kind":"loot","x":10,"y":0,"z":1,"lootUnitValue":2798,"lootValueKnown":true,"lootValueSource":"shop-dynamic-guide","count":99},
                {"id":"TEST-value-boundary","name":"TEST BOUNDARY","kind":"loot","x":11,"y":0,"z":1,"lootUnitValue":20000,"lootValueKnown":true,"lootValueSource":"shop-dynamic-guide"},
                {"id":"TEST-value-high","name":"TEST GOLD PRICE","kind":"loot","x":12,"y":0,"z":1,"lootUnitValue":57757,"lootValueKnown":true,"lootValueSource":"shop-dynamic-guide"},
                {"id":"TEST-value-container","name":"TEST CONTAINER ITEM","kind":"loot","x":13,"y":0,"z":1,"lootUnitValue":25000,"lootValueKnown":true,"lootValueSource":"shop-dynamic-guide","lootContainer":true},
                {"id":"TEST-value-unknown","name":"TEST UNKNOWN","kind":"loot","x":14,"y":0,"z":1,"lootUnitValue":90000,"lootValueKnown":false,"lootValueSource":"unknown"},
                {"id":"TEST-value-static","name":"TEST STATIC PRICE","kind":"loot","x":15,"y":0,"z":1,"lootUnitValue":90000,"lootValueKnown":true,"lootValueSource":"initial-guide"},
                {"id":"TEST-value-unopened","name":"TEST UNOPENED CONTAINER","kind":"loot","x":16,"y":1,"z":1,"lootContainer":true,"lootValueKnown":false}] }""".trimIndent()
            val service = EntityFeedService.instance!!
            feeder = thread(isDaemon = true) {
                while (keepFeeding.get()) { service.ingest(raw); SystemClock.sleep(20) }
            }
            fun projected(): Set<String>? = try {
                val json = JSONObject(output.readText())
                if (System.currentTimeMillis() - json.getLong("captureWallTimeMs") > 1500) null
                else json.getJSONArray("projected").let { list ->
                    (0 until list.length()).map { list.getJSONObject(it).getString("id") }.toSet()
                }
            } catch (_: Exception) { null }
            await("20k filter did not keep only qualifying individual items") {
                projected() == setOf("TEST-value-high", "TEST-value-container")
            }
            Settings.setLootValueFilter(context, false)
            await("all-loot mode did not preserve unknown/cheap items") { projected()?.size == 7 }
            Settings.setLootValueFilter(context, true)
            Settings.setLootMinimumValue(context, 50_000)
            await("50k threshold did not keep only the gold-price fixture") { projected() == setOf("TEST-value-high") }
            Settings.setContainerGuide(context, true)
            await("guide did not retain unopened and lower-value containers alongside valuable loot") {
                projected() == setOf("TEST-value-high", "TEST-value-container", "TEST-value-unopened")
            }
            Settings.setEntityFeatures(context, Settings.EntityType.LOOT, 0)
            await("container guide must work with loose loot OFF") {
                projected() == setOf("TEST-value-container", "TEST-value-unopened") &&
                    OverlayService.instance?.overlayVisible == true
            }
            Settings.setContainerGuide(context, false)
            await("guide OFF and loot OFF must hide both containers") { projected() == emptySet<String>() }
        } finally {
            keepFeeding.set(false); feeder?.join(1000)
            instrumentation.runOnMainSync { OverlayService.stop(context); EntityFeedService.stop(context) }
            Settings.setEntityFeatures(context, Settings.EntityType.LOOT, features)
            Settings.setLootValueFilter(context, filter)
            Settings.setLootMinimumValue(context, minimum)
            Settings.setLootDrawMode(context, mode)
            Settings.setContainerGuide(context, guide)
            if (!captureBefore) capture.delete()
        }
    }
    @Test fun valuableSafeBelowViewHasVisibleBodyOnlyEdgeMarker() {
        assumeTrue(AndroidSettings.canDrawOverlays(context))
        val guide = Settings.containerGuide(context)
        val features = Settings.entityFeatures(context, Settings.EntityType.LOOT)
        val filter = Settings.lootValueFilter(context)
        val minimum = Settings.lootMinimumValue(context)
        val mode = Settings.lootDrawMode(context)
        val color = Settings.lootColor(context)
        val capture = File(context.filesDir, "capture-feed.enabled")
        val captureBefore = capture.isFile
        val output = File(context.filesDir, "latest-overlay-frame.json")
        val keepFeeding = AtomicBoolean(true)
        var feeder: Thread? = null
        try {
            capture.writeText("")
            Settings.setContainerGuide(context, false)
            Settings.setEntityFeatures(context, Settings.EntityType.LOOT,
                Settings.FEATURE_ENABLED or Settings.FEATURE_BODY)
            Settings.setLootDrawMode(context, Settings.DRAW_MODE_BOXES)
            Settings.setLootColor(context, 0xFF00FF30.toInt())
            Settings.setLootValueFilter(context, true)
            Settings.setLootMinimumValue(context, 20_000)
            instrumentation.runOnMainSync { EntityFeedService.start(context); OverlayService.show(context) }
            await("services not ready") { EntityFeedService.instance?.isFeedRunning == true && OverlayService.instance != null }
            // Reproduce the captured safe/camera offset without game memory.
            val raw = """{"self":"TEST-safe-camera","entities":[
                {"id":"TEST-safe-camera","x":0,"y":0,"z":1.1,"yaw":-78.7,"pitch":-4.9,"fov":112.5},
                {"id":"TEST-safe-edge","kind":"loot","name":"TEST SAFE EDGE","x":-0.1,"y":-0.9,"z":0,
                 "lootContainer":true,"lootUnitValue":2050520,"lootValueKnown":true,"lootValueSource":"shop-dynamic-guide"}]}""".trimIndent()
            val service = EntityFeedService.instance!!
            feeder = thread(isDaemon = true) {
                while (keepFeeding.get()) { service.ingest(raw); SystemClock.sleep(20) }
            }
            var pointX = 0f
            var pointY = 0f
            await("safe was not projected to the screen edge") {
                try {
                    val json = JSONObject(output.readText())
                    val list = json.getJSONArray("projected")
                    if (System.currentTimeMillis() - json.getLong("captureWallTimeMs") > 1500 || list.length() != 1) false
                    else list.getJSONObject(0).let { e ->
                        pointX = e.getDouble("x").toFloat(); pointY = e.getDouble("y").toFloat()
                        e.getString("id") == "TEST-safe-edge" && !e.getBoolean("onScreen")
                    }
                } catch (_: Exception) { false }
            }
            fun edgePixels(name: String): Int {
                instrumentation.waitForIdleSync()
                val bitmap = instrumentation.uiAutomation.takeScreenshot()
                val cx = (pointX * bitmap.width).toInt()
                val cy = (pointY * bitmap.height).toInt()
                val radius = (24 * context.resources.displayMetrics.density).toInt()
                var pixels = 0
                for (x in (cx - radius).coerceAtLeast(0)..(cx + radius).coerceAtMost(bitmap.width - 1)) {
                    for (y in (cy - radius).coerceAtLeast(0)..(cy + radius).coerceAtMost(bitmap.height - 1)) {
                        val pixel = bitmap.getPixel(x, y)
                        if (android.graphics.Color.green(pixel) > 200 && android.graphics.Color.red(pixel) < 80 &&
                            android.graphics.Color.blue(pixel) < 180) pixels++
                    }
                }
                File(context.filesDir, name).outputStream().use {
                    bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, it)
                }
                bitmap.recycle()
                return pixels
            }
            await("overlay did not draw") { OverlayService.instance?.overlayVisible == true && OverlayService.instance!!.overlayDrawnFrames > 0 }
            SystemClock.sleep(150)
            val shown = edgePixels("test-safe-edge-shown.png")
            instrumentation.runOnMainSync { OverlayService.hide(context) }
            await("Hide did not hide overlay") { OverlayService.instance?.overlayVisible == false }
            SystemClock.sleep(150)
            val hidden = edgePixels("test-safe-edge-hidden.png")
            assertTrue("Body-only offscreen loot marker must add visible green pixels: shown=$shown hidden=$hidden", shown > hidden + 20)
        } finally {
            keepFeeding.set(false); feeder?.join(1000)
            instrumentation.runOnMainSync { OverlayService.stop(context); EntityFeedService.stop(context) }
            Settings.setEntityFeatures(context, Settings.EntityType.LOOT, features)
            Settings.setLootValueFilter(context, filter)
            Settings.setLootMinimumValue(context, minimum)
            Settings.setLootDrawMode(context, mode)
            Settings.setLootColor(context, color)
            Settings.setContainerGuide(context, guide)
            if (!captureBefore) capture.delete()
        }
    }

}
