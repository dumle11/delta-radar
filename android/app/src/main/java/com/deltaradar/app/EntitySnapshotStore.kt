package com.deltaradar.app

import org.json.JSONArray
import org.json.JSONObject

/** Complete scanner frames, committed atomically and aged with a monotonic clock. */
class EntitySnapshotStore(private val clock: () -> Long) {
    companion object { const val FRESHNESS_MS = 750L }

    data class Status(
        val receivedAtMs: Long, val sequence: Long, val fresh: Boolean,
        val hasCamera: Boolean, val entityCount: Int, val worldGeneration: Long = 0
    )
    data class Snapshot(val json: JSONObject, val status: Status)

    private var frame = JSONObject().put("entities", JSONArray())
    private var receivedAtMs = -1L
    private var sequence = 0L
    private var worldGeneration = 0L

    @Synchronized fun ingest(line: String): Boolean {
        // Validate the entire frame before changing the visible snapshot or its age.
        val next = try {
            val input = JSONObject(line)
            val list = input.getJSONArray("entities")
            val self = input.optString("self", "")
            val ids = HashSet<String>()
            val accepted = JSONArray()
            var cameraFound = false
            for (i in 0 until list.length()) {
                val entity = list.getJSONObject(i)
                val id = entity.getString("id")
                require(id.isNotBlank() && ids.add(id))
                for (key in listOf("x", "y", "z")) require(entity.getDouble(key).isFinite())
                for (key in listOf("yaw", "pitch", "fov", "health", "maxHealth", "halfHeight")) {
                    if (entity.has(key) && !entity.isNull(key)) require(entity.getDouble(key).isFinite())
                }
                if (entity.has("bones")) {
                    val bones = entity.getJSONArray("bones")
                    require(bones.length() == 16)
                    for (b in 0 until bones.length()) {
                        val bone = bones.getJSONArray(b)
                        require(bone.length() == 3)
                        for (axis in 0..2) require(bone.getDouble(axis).isFinite())
                    }
                }
                if (id == self) {
                    for (key in listOf("yaw", "pitch", "fov")) require(entity.getDouble(key).isFinite())
                    require(entity.getDouble("fov") in 5.0..180.0)
                    cameraFound = true
                }
                val dead = entity.optBoolean("dead", false) ||
                    (entity.optBoolean("healthKnown", false) && entity.optDouble("health", 100.0) <= 0.01)
                if (!dead || id == self) accepted.put(entity)
            }
            // An empty frame explicitly ends the old world. A nonempty frame
            // without its camera cannot establish a valid rendering session.
            require((list.length() == 0 && self.isEmpty()) || (self.isNotEmpty() && cameraFound))
            JSONObject().put("entities", accepted).also { if (self.isNotEmpty()) it.put("self", self) }
        } catch (_: Exception) { return false }
        if (next.optString("self", "") != frame.optString("self", "") || !next.has("self")) worldGeneration++
        frame = next
        receivedAtMs = clock()
        sequence++
        return true
    }

    @Synchronized fun status(): Status {
        val age = clock() - receivedAtMs
        val fresh = receivedAtMs >= 0 && age >= 0 && age < FRESHNESS_MS
        return Status(receivedAtMs, sequence, fresh,
            fresh && frame.has("self"), if (fresh) frame.getJSONArray("entities").length() else 0, worldGeneration)
    }

    @Synchronized fun snapshot(): Snapshot {
        val status = status()
        val copy = if (status.fresh) JSONObject(frame.toString())
            else JSONObject().put("entities", JSONArray())
        return Snapshot(copy, status)
    }

    @Synchronized fun isCurrent(candidate: Status): Boolean {
        val current = status()
        return current.fresh && current.hasCamera && current.sequence == candidate.sequence
    }

    /** A newer camera sample from this world must not starve an in-flight draw.
     * Empty frames, disconnects and a different camera identity invalidate it. */
    @Synchronized fun canRender(candidate: Status): Boolean {
        val current = status()
        val age = clock() - candidate.receivedAtMs
        return current.hasCamera && candidate.hasCamera && age in 0 until FRESHNESS_MS &&
            current.worldGeneration == candidate.worldGeneration && candidate.sequence <= current.sequence
    }

    @Synchronized fun clear() {
        frame = JSONObject().put("entities", JSONArray())
        receivedAtMs = -1
        sequence++
        worldGeneration++
    }
}
