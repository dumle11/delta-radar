package com.deltaradar.app

import java.io.File
import java.io.IOException
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class PositionTraceTest {
    @get:Rule val folder = TemporaryFolder()

    @Test fun snapshotUsesWhitelistAndRetainsPositionMetadata() {
        val source = JSONObject("""
            {
              "self":"ent_12", "cameraSampledAt":100, "cameraTimestamp":3.0,
              "cameraPositionPlaintext":true, "secret":"secret",
              "diagnostics":{"gworld":"0x1234","actorCount":2,"name":"Alice"},
              "entities":[{
                "id":"ent_abcd","kind":"player","name":"Alice","x":11.5,"y":12,"z":13,
                "positionPlaintext":false,
                "positionTrace":{"root":"0x1234","boundsIndex":5,"boundsEncrypted":1,
                                 "worldIndex":-1,"name":"Alice"}
              }]
            }
        """.trimIndent())
        val text = PositionTraceSnapshot.format(source, "session", 1, 2, 3, 0)
        assertFalse(text.contains("Alice"))
        assertFalse(text.contains("secret"))
        val result = JSONObject(text)
        assertEquals(2, result.getInt("schema"))
        assertEquals("ent_12", result.getString("self"))
        assertEquals(2, result.getJSONObject("diagnostics").getInt("actorCount"))
        val entity = result.getJSONArray("entities").getJSONObject(0)
        assertEquals(11.5, entity.getDouble("x"), 0.001)
        val position = entity.getJSONObject("positionTrace")
        assertEquals(5, position.getInt("boundsIndex"))
        assertEquals(1, position.getInt("boundsEncrypted"))
        assertEquals(-1, position.getInt("worldIndex"))
        assertEquals("0x1234", position.getString("root"))
    }

    @Test fun rotationAndRecordSizeAreBounded() {
        val file = PositionTraceFile(folder.root, 12)
        file.append("first")
        file.append("second")
        assertEquals("first\n", file.previous.readText())
        assertEquals("second\n", file.current.readText())
        file.append("third")
        assertEquals("second\n", file.previous.readText())
        assertEquals("third\n", file.current.readText())
        try {
            file.append("012345678901")
            fail("oversized record should fail")
        } catch (_: IOException) {
            assertEquals("third\n", file.current.readText())
        }
    }

    @Test fun missingEntitiesAndTruncationAreExplicit() {
        val diagnosticsOnly = JSONObject(PositionTraceSnapshot.format(
            JSONObject("""{"diagnostics":{"status":"world unavailable","readFailures":2}}"""),
            "session", 1, 2, 3, 0))
        assertTrue(diagnosticsOnly.isNull("entityCount"))
        assertEquals(2, diagnosticsOnly.getJSONObject("diagnostics").getInt("readFailures"))
        assertEquals("world unavailable", diagnosticsOnly.getJSONObject("diagnostics").getString("status"))
        val list = org.json.JSONArray()
        repeat(513) {
            list.put(JSONObject().put("id", "ent_1").put("kind", "player"))
            list.put(JSONObject().put("kind", "loot").put("name", "private"))
        }
        val output = JSONObject(PositionTraceSnapshot.format(JSONObject().put("entities", list)
            .put("entitiesTruncated", true), "session", 1, 2, 3, 0))
        assertTrue(output.getBoolean("entitiesTruncated"))
        assertTrue(output.getBoolean("entitiesTruncatedInTrace"))
        assertEquals(1026, output.getInt("entityCount"))
        assertEquals(513, output.getInt("omittedLoot"))
        assertEquals(1, output.getInt("omittedOtherEntities"))
        assertEquals(512, output.getJSONArray("entities").length())
        assertFalse(output.toString().contains("private"))
    }

    @Test fun recordingIsOptInThrottledAndExpires() {
        val file = PositionTraceFile(folder.root)
        val recorder = PositionTraceRecorder(file) { _, e -> throw AssertionError(e) }
        val snapshot = JSONObject("""{"entities":[{"id":"ent_1","kind":"player"}]}""")
        recorder.offer(snapshot, 1000, 1000)
        assertFalse(file.current.exists())
        assertTrue(recorder.start(1000, 1000))
        recorder.offer(snapshot, 1000, 1000)
        recorder.offer(snapshot, 1500, 1500)
        waitUntil { recorder.status.startsWith("Recording; samples") }
        recorder.offer(snapshot, 2000, 2000)
        waitUntil { file.current.readLines().size == 2 }
        recorder.expire(1000 + PositionTraceRecorder.DURATION_MS)
        assertFalse(recorder.isRecording())
        recorder.offer(snapshot, 3000, 1000 + PositionTraceRecorder.DURATION_MS + 1000)
        recorder.close()
        assertEquals(2, file.current.readLines().size)
    }

    @Test fun writeFailureStopsAndReportsCapture() {
        val latch = CountDownLatch(1)
        val file = PositionTraceFile(File(folder.root, "missing-directory"))
        val recorder = PositionTraceRecorder(file) { _, _ -> latch.countDown() }
        assertTrue(recorder.start(1000, 1000))
        recorder.offer(JSONObject("""{"entities":[]}"""), 1000, 1000)
        assertTrue(latch.await(5, TimeUnit.SECONDS))
        assertFalse(recorder.isRecording())
        assertTrue(recorder.status.contains("error"))
        recorder.close()
    }

    private fun waitUntil(condition: () -> Boolean) {
        val deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(5)
        while (!condition() && System.nanoTime() < deadline) Thread.sleep(10)
        assertTrue("timed out waiting for trace writer", condition())
    }
}
