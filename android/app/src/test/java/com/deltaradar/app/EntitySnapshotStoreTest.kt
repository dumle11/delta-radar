package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class EntitySnapshotStoreTest {
    @Test fun olderStillFreshFrameCannotRewindCameraOrClearTheWorld() {
        now = 1_000
        assertTrue(store.ingest(frame(entity("current")).replaceFirst("{", "{\"capturedElapsedMs\":900,")))
        val current = store.snapshot()
        now = 1_100
        for (older in listOf(frame(entity("old")), """{"self":"","entities":[]}""")) {
            assertFalse(store.ingest(older.replaceFirst("{", "{\"capturedElapsedMs\":850,")))
            assertEquals(current.status.sequence, store.status().sequence)
            assertEquals(900L, store.status().receivedAtMs)
        }
        assertTrue(store.snapshot().json.toString().contains("current"))
    }

    @Test fun oversizedFrameOrEntityArrayCannotReplaceValidData() {
        assertTrue(store.ingest(frame(entity("current"))))
        val sequence = store.status().sequence
        assertFalse(store.ingest(" ".repeat(EntitySnapshotStore.MAX_FRAME_CHARS + 1)))
        val tooMany = Array(EntitySnapshotStore.MAX_ENTITIES) { entity("extra-$it") }
        assertFalse(store.ingest(frame(*tooMany))) // Includes the camera as well.
        assertEquals(sequence, store.status().sequence)
    }
    @Test fun transportDelayCountsAgainstFreshnessInsteadOfRenewingIt() {
        now = 600
        val captured = frame(entity("enemy")).replaceFirst("{", "{\"capturedElapsedMs\":100,")
        assertTrue(store.ingest(captured))
        assertEquals(100L, store.status().receivedAtMs)
        now = 850
        assertFalse(store.status().fresh)
    }

    @Test fun bufferedAndFutureFramesCannotReplaceTheCurrentSnapshot() {
        now = 1_000
        assertTrue(store.ingest(frame(entity("current"))))
        val sequence = store.status().sequence
        for (capture in listOf(250L, 1_001L, -1L)) {
            val bad = frame(entity("old")).replaceFirst("{", "{\"capturedElapsedMs\":$capture,")
            assertFalse(store.ingest(bad))
            assertEquals(sequence, store.status().sequence)
        }
        assertTrue(store.snapshot().json.toString().contains("current"))
    }
    private var now = 0L
    private val store = EntitySnapshotStore { now }
    private fun entity(id: String, extra: String = "") =
        """{"id":"$id","x":10,"y":20,"z":30,"yaw":0,"pitch":0,"fov":80$extra}"""
    private fun frame(vararg enemies: String) =
        """{"self":"self","entities":[${(listOf(entity("self")) + enemies).joinToString(",") }]}"""

    @Test fun listenerWithoutFrameCannotBeConnected() {
        assertFalse(store.status().fresh)
        assertFalse(store.status().hasCamera)
    }
    @Test fun completeFramesRemoveDespawnedEntitiesImmediately() {
        assertTrue(store.ingest(frame(entity("enemy"))))
        assertEquals(2, store.status().entityCount)
        assertTrue(store.ingest(frame()))
        assertEquals(1, store.status().entityCount)
    }
    @Test fun loadingFrameClearsPreviousWorldImmediately() {
        store.ingest(frame(entity("enemy")))
        val old = store.status()
        assertTrue(store.ingest("""{"self":"","entities":[]}"""))
        assertEquals(0, store.status().entityCount)
        assertTrue(store.status().fresh)
        assertFalse(store.status().hasCamera)
        assertFalse(store.isCurrent(old))
    }
    @Test fun stalledSocketExpiresWithoutAnotherIncomingFrame() {
        store.ingest(frame(entity("enemy")))
        val old = store.status()
        now = EntitySnapshotStore.FRESHNESS_MS - 1
        assertTrue(store.status().hasCamera)
        now++
        assertFalse(store.status().fresh)
        assertEquals(0, store.status().entityCount)
        assertFalse(store.snapshot().json.has("self"))
        assertEquals(0, store.snapshot().json.getJSONArray("entities").length())
        assertFalse(store.isCurrent(old))
    }
    @Test fun invalidFramesDoNotPartiallyReplaceDataOrRenewFreshness() {
        store.ingest(frame(entity("enemy")))
        val old = store.status()
        now = 500
        assertFalse(store.ingest(frame("""{"id":"broken","x":1,"y":2}""")))
        assertEquals(old, store.status())
        now = 750
        assertFalse(store.status().fresh)
    }
    @Test fun missingCameraDuplicateIdsAndNonfiniteCoordinatesAreRejected() {
        assertFalse(store.ingest("""{"self":"absent","entities":[${entity("enemy")}]}"""))
        assertFalse(store.ingest(frame(entity("enemy"), entity("enemy"))))
        assertFalse(store.ingest(frame(entity("enemy").replace("\"x\":10", "\"x\":\"NaN\""))))
        assertFalse(store.ingest(frame(entity("enemy", ",\"bones\":[[0,0,0]]"))))
        assertFalse(store.ingest(frame(entity("enemy", ",\"health\":\"NaN\""))))
        assertFalse(store.status().fresh)
    }
    @Test fun explicitDeathRemovesEnemyAndKeepsSpectatorCamera() {
        assertTrue(store.ingest("""{"self":"self","entities":[${entity("self", ",\"dead\":true")},${entity("enemy", ",\"dead\":true")}]}"""))
        assertEquals(1, store.status().entityCount)
        assertTrue(store.status().hasCamera)
    }
    @Test fun disconnectInvalidatesPendingFrameAndReconnectStartsFresh() {
        store.ingest(frame(entity("old")))
        val old = store.status()
        store.clear()
        assertFalse(store.isCurrent(old))
        assertEquals(0, store.status().entityCount)
        assertFalse(store.status().fresh)
        now++
        store.ingest(frame(entity("new")))
        assertTrue(store.status().hasCamera)
        assertFalse(store.isCurrent(old))
        assertTrue(store.snapshot().json.toString().contains("new"))
        assertFalse(store.snapshot().json.toString().contains("old"))
    }
    @Test fun callersCannotMutateStoredSnapshot() {
        store.ingest(frame(entity("enemy")))
        store.snapshot().json.getJSONArray("entities").getJSONObject(0).put("x", 999)
        assertEquals(10.0, store.snapshot().json.getJSONArray("entities").getJSONObject(0).getDouble("x"), 0.0)
    }
    @Test fun newSnapshotRejectsOlderQueuedProjection() {
        store.ingest(frame(entity("enemy")))
        val old = store.status()
        store.ingest(frame())
        assertFalse(store.isCurrent(old))
        assertTrue(store.isCurrent(store.status()))
    }
    @Test fun newerSameWorldSampleDoesNotStarveQueuedRendering() {
        store.ingest(frame(entity("enemy")))
        val queued = store.status()
        now = 10
        store.ingest(frame(entity("enemy")))
        assertFalse(store.isCurrent(queued))
        assertTrue(store.canRender(queued))
    }
    @Test fun loadingAndDisconnectInvalidateOldWorkEvenAfterFreshInput() {
        store.ingest(frame(entity("old")))
        val queued = store.status()
        store.ingest("""{"self":"","entities":[]}""")
        store.ingest(frame(entity("new")))
        assertFalse(store.canRender(queued))
        val beforeDisconnect = store.status()
        store.clear()
        store.ingest(frame(entity("new")))
        assertFalse(store.canRender(beforeDisconnect))
    }
    @Test fun cameraReplacementInvalidatesQueuedRendering() {
        store.ingest(frame(entity("enemy")))
        val queued = store.status()
        store.ingest("""{"self":"replacement","entities":[${entity("replacement")}]}""")
        assertFalse(store.canRender(queued))
    }
    @Test fun freshIncomingFramesCannotRenewAnOldProjection() {
        store.ingest(frame(entity("enemy")))
        val queued = store.status()
        now = EntitySnapshotStore.FRESHNESS_MS
        store.ingest(frame(entity("enemy")))
        assertTrue(store.status().hasCamera)
        assertFalse(store.canRender(queued))
    }

}
