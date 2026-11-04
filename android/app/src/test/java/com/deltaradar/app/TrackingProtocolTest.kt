package com.deltaradar.app

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class TrackingProtocolTest {
    private fun camera(at: Long = 1000, stamp: Double = 1.0) = LiveCamera(
        42, "0x1234", at, stamp, true, 10.0, 20.0, 30.0, 90.0, 0.0, 5.0, 80.0)
    private fun packet(at: Long = 1000, stamp: Double = 1.0) = JSONObject()
        .put("gamePid",42).put("world","0x1234").put("sampledElapsedMs",at).put("timestamp",stamp)
        .put("valid",true).put("x",10).put("y",20).put("z",30).put("yaw",90).put("pitch",0).put("roll",5).put("fov",80)
    private fun frame(world: String = "0x1234"): JSONObject {
        val self = JSONObject().put("id","self").put("x",0).put("y",0).put("z",0)
            .put("yaw",0).put("pitch",0).put("fov",80)
        val enemy = JSONObject().put("id","enemy").put("kind","player").put("x",10).put("y",20).put("z",30)
            .put("bonesVerified",true).put("boneCount",17)
            .put("bones",JSONArray().also { bones -> repeat(17) { bones.put(JSONArray(listOf(10,20,it))) } })
            .put("boneParents",JSONArray().also { parents -> repeat(17) { parents.put(it-1) } })
        return JSONObject().put("gamePid",42).put("world",world).put("self","self")
            .put("entities",JSONArray().put(self).put(enemy))
    }
    @Test fun cameraRequiresMatchingProcessWorldAndFreshMonotonicSample() {
        val c=camera()
        assertTrue(c.usable(42,"0x1234",1100))
        assertFalse(c.usable(43,"0x1234",1000))
        assertFalse(c.usable(42,"0x9876",1000))
        assertFalse(c.usable(42,"0x1234",1101))
        assertFalse(c.usable(42,"0x1234",999))
        assertFalse(c.copy(roll=Double.NaN).usable(42,"0x1234",1000))
    }
    @Test fun cameraCannotRewindAndDuplicatePacketsDoNotScheduleMoreProjection() {
        val store=CameraSnapshotStore()
        store.ingest(packet().toString(),1000)
        val sequence=store.snapshot().sequence
        store.ingest(packet().toString(),1001)
        assertEquals(sequence,store.snapshot().sequence)
        try {store.ingest(packet(999,0.9).toString(),1001);fail("rewound camera accepted")} catch (_: IllegalArgumentException) {}
        assertEquals(1000L,store.snapshot().camera!!.sampledAt)
        store.ingest(packet(1010,1.1).toString(),1010)
        assertTrue(store.snapshot().sequence>sequence)
        store.ingest(packet().put("valid",false).toString(),1011)
        assertNull(store.snapshot().camera)
    }
    @Test fun sameEntityFrameCanBeProjectedWithANewerCamera() {
        val gate=FramePublicationGate();val source=Any()
        assertTrue(gate.accept(source,1,5,10))
        assertFalse(gate.accept(source,1,5,10))
        assertTrue(gate.accept(source,1,5,11))
        assertFalse(gate.accept(source,1,4,12))
        assertTrue(gate.accept(source,1,6,11))
    }
    @Test fun idleHeartbeatCannotHideAStaleOrFutureProducer() {
        val store = CameraSnapshotStore()
        assertEquals(1000L, store.ingest(packet().put("valid",false).toString(),1001))
        for (sample in listOf(0L, 1L, 4001L)) {
            try {
                store.ingest(packet(sample).put("valid",false).toString(),4000)
                fail("invalid liveness timestamp accepted: $sample")
            } catch (_: IllegalArgumentException) {}
        }
    }
    @Test fun compactPoseAndLootMetadataSurviveWhileInvalidHierarchyIsRejectedAtomically() {
        val store=EntitySnapshotStore {1000}
        val valid=frame();valid.getJSONArray("entities").getJSONObject(1)
            .put("lootUnitValue",25000).put("lootValueSource","test-source")
        assertTrue(store.ingest(valid.toString()))
        val accepted=store.snapshot()
        assertEquals(17,accepted.json.getJSONArray("entities").getJSONObject(1).getJSONArray("bones").length())
        assertEquals(25000,accepted.json.getJSONArray("entities").getJSONObject(1).getInt("lootUnitValue"))
        val invalid=frame();invalid.getJSONArray("entities").getJSONObject(1).getJSONArray("boneParents").put(2,3)
        assertFalse(store.ingest(invalid.toString()))
        assertEquals(accepted.status.sequence,store.status().sequence)
        val mismatched=frame();mismatched.getJSONArray("entities").getJSONObject(1).put("boneCount",19)
        assertFalse(store.ingest(mismatched.toString()))
    }
    @Test fun sameSelfIdInAnotherWorldOrProcessInvalidatesQueuedProjection() {
        val store=EntitySnapshotStore {1000}
        assertTrue(store.ingest(frame().toString()));val queued=store.status()
        assertTrue(store.ingest(frame("0x9999").toString()))
        assertFalse(store.canRender(queued))
        val oldProcess=store.status()
        assertTrue(store.ingest(frame("0x9999").put("gamePid",43).toString()))
        assertFalse(store.canRender(oldProcess))
    }
}
