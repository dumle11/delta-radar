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
            Settings.setEntityFeatures(context, Settings.EntityType.LOOT, Settings.ALL_FEATURES)
            Settings.setLootDrawMode(context, Settings.DRAW_MODE_BOXES)
            Settings.setLootValueFilter(context, true)
            Settings.setLootMinimumValue(context, 20_000)
            instrumentation.runOnMainSync { EntityFeedService.start(context); OverlayService.show(context) }
            await("services not ready") { EntityFeedService.instance?.isFeedRunning == true && OverlayService.instance != null }
            val raw = """{"self":"TEST-value-camera","entities":[
                {"id":"TEST-value-camera","x":0,"y":0,"z":1.6,"yaw":0,"pitch":0,"fov":80},
                {"id":"TEST-value-cheap-stack","name":"TEST CHEAP STACK","kind":"loot","x":10,"y":0,"z":1,"lootUnitValue":2798,"lootValueKnown":true,"lootValueSource":"auction-cache","count":99},
                {"id":"TEST-value-boundary","name":"TEST BOUNDARY","kind":"loot","x":11,"y":0,"z":1,"lootUnitValue":20000,"lootValueKnown":true,"lootValueSource":"auction-cache"},
                {"id":"TEST-value-high","name":"TEST HIGH","kind":"loot","x":12,"y":0,"z":1,"lootUnitValue":20001,"lootValueKnown":true,"lootValueSource":"auction-cache"},
                {"id":"TEST-value-container","name":"TEST CONTAINER ITEM","kind":"loot","x":13,"y":0,"z":1,"lootUnitValue":25000,"lootValueKnown":true,"lootValueSource":"auction-cache","lootContainer":true},
                {"id":"TEST-value-unknown","name":"TEST UNKNOWN","kind":"loot","x":14,"y":0,"z":1,"lootUnitValue":90000,"lootValueKnown":false,"lootValueSource":"unknown"},
                {"id":"TEST-value-static","name":"TEST STATIC PRICE","kind":"loot","x":15,"y":0,"z":1,"lootUnitValue":90000,"lootValueKnown":true,"lootValueSource":"initial-guide"}] }""".trimIndent()
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
            await("all-loot mode did not preserve unknown/cheap items") { projected()?.size == 6 }
            Settings.setLootValueFilter(context, true)
            Settings.setLootMinimumValue(context, 50_000)
            await("adjusted threshold did not remove lower-valued items") { projected() == emptySet<String>() }
        } finally {
            keepFeeding.set(false); feeder?.join(1000)
            instrumentation.runOnMainSync { OverlayService.stop(context); EntityFeedService.stop(context) }
            Settings.setEntityFeatures(context, Settings.EntityType.LOOT, features)
            Settings.setLootValueFilter(context, filter)
            Settings.setLootMinimumValue(context, minimum)
            Settings.setLootDrawMode(context, mode)
            if (!captureBefore) capture.delete()
        }
    }
}
