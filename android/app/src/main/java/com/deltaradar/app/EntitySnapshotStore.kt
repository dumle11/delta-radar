package com.deltaradar.app

import org.json.JSONArray
import org.json.JSONObject

/** Complete scanner frames, committed atomically and aged with a monotonic clock. */
class EntitySnapshotStore(private val clock: () -> Long) {
    companion object {
        const val FRESHNESS_MS = 750L
        // Current scanner bounds are 512 actors and roughly 1 MiB of JSON.
        const val MAX_FRAME_CHARS = 2 * 1024 * 1024
        const val MAX_ENTITIES = 1024
    }

    data class Status(
        val receivedAtMs: Long, val sequence: Long, val fresh: Boolean,
        val hasCamera: Boolean, val entityCount: Int, val worldGeneration: Long = 0
    )
    data class Snapshot(val json: JSONObject, val status: Status) {
        internal var camera: LiveCamera? = null
        internal var cameraRequired = false
        internal var cameraSequence = 0L
    }

    private var frame = JSONObject().put("entities", JSONArray())
    private var receivedAtMs = -1L
    private var sequence = 0L
    private var worldGeneration = 0L

    @Synchronized fun ingest(line: String): Boolean {
        if (line.length > MAX_FRAME_CHARS) return false
        // Validate the entire frame before changing the visible snapshot or its age.
        val arrivedAt = clock()
        var capturedAt = arrivedAt
        val next = try {
            val input = JSONObject(line)
            if (input.has("capturedElapsedMs")) {
                // Native CLOCK_BOOTTIME and Android elapsedRealtime share the
                // device boot clock. Socket backlog must not rejuvenate old data.
                capturedAt = input.getLong("capturedElapsedMs")
                require(capturedAt >= 0 && arrivedAt - capturedAt in 0 until FRESHNESS_MS)
                require(capturedAt >= receivedAtMs) // Reject an older still-fresh buffered frame.
            }
            val list = input.getJSONArray("entities")
            require(list.length() <= MAX_ENTITIES)
            val self = input.optString("self", "")
            val sameWorld = input.optInt("gamePid") == frame.optInt("gamePid") &&
                input.optString("world") == frame.optString("world")
            val previous = HashMap<String, JSONObject>()
            if (sameWorld) frame.getJSONArray("entities").let { old ->
                for (i in 0 until old.length()) old.getJSONObject(i).let { previous[it.getString("id")] = it }
            }
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
                if (entity.has("sampledElapsedMs")) {
                    val sampled = entity.getLong("sampledElapsedMs")
                    require(sampled >= 0 && sampled <= arrivedAt)
                    val old = previous[id]
                    if (old != null && old.optString("name") == entity.optString("name")) {
                        val velocity = WorldMotion.velocity(entity.getDouble("x"), entity.getDouble("y"), entity.getDouble("z"), sampled,
                            old.getDouble("x"), old.getDouble("y"), old.getDouble("z"), old.optLong("sampledElapsedMs"))
                        entity.put("velocityX", velocity.x).put("velocityY", velocity.y).put("velocityZ", velocity.z)
                    }
                }
                if (entity.has("bones")) {
                    val bones = entity.getJSONArray("bones")
                    val verified = entity.optBoolean("bonesVerified", false)
                    require(if (verified) bones.length() in 2..SkeletonProjection.MAX_BONES else bones.length() == 16)
                    if (verified) {
                        require(entity.getInt("boneCount") == bones.length())
                        val parents = entity.getJSONArray("boneParents")
                        require(parents.length() == bones.length() && parents.getInt(0) == -1)
                        for (b in 1 until parents.length()) require(parents.getInt(b) in 0 until b)
                    }
                    for (b in 0 until bones.length()) {
                        val bone = bones.getJSONArray(b)
                        require(bone.length() == 3)
                        for (axis in 0..2) require(bone.getDouble(axis).isFinite())
                    }
                } else require(!entity.optBoolean("bonesVerified", false))
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
            JSONObject().put("entities", accepted).also { next ->
                if (self.isNotEmpty()) next.put("self", self)
                for (key in listOf("gamePid", "world", "cameraSampledElapsedMs", "cameraTimestamp", "cameraPositionPlaintext", "cameraRoll"))
                    if (input.has(key)) next.put(key, input.get(key))
            }
        } catch (_: Exception) { return false }
        if (next.optString("self", "") != frame.optString("self", "") || !next.has("self") ||
            next.optInt("gamePid") != frame.optInt("gamePid") || next.optString("world") != frame.optString("world")) worldGeneration++
        frame = next
        receivedAtMs = capturedAt
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

    /** Owned immutable frame for the projection reader; callers must never mutate it. */
    @Synchronized internal fun projectionSnapshot() = Snapshot(
        if (status().fresh) frame else JSONObject().put("entities", JSONArray()), status())

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
