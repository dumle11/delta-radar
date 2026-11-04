package com.deltaradar.app

import java.io.File
import java.io.IOException
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.RejectedExecutionException
import org.json.JSONArray
import org.json.JSONException
import org.json.JSONObject

internal class PositionTraceFile(
    directory: File,
    private val maxFileBytes: Long = 2L * 1024 * 1024
) {
    companion object { private val fileLock = Any() }
    val current = File(directory, "position-trace-current.jsonl")
    val previous = File(directory, "position-trace-previous.jsonl")

    @Throws(IOException::class)
    fun append(line: String) = synchronized(fileLock) {
        val bytes = (line + "\n").toByteArray(Charsets.UTF_8)
        if (bytes.size > maxFileBytes) throw IOException("Trace record exceeds file limit")
        if (current.length() > maxFileBytes) throw IOException("Current trace exceeds file limit")
        if (previous.length() > maxFileBytes) throw IOException("Previous trace exceeds file limit")
        if (current.length() + bytes.size > maxFileBytes) {
            if (previous.exists() && !previous.delete()) throw IOException("Cannot rotate previous trace")
            if (current.exists() && !current.renameTo(previous)) throw IOException("Cannot rotate current trace")
        }
        current.appendBytes(bytes)
    }
}

internal object PositionTraceSnapshot {
    private val idPattern = Regex("^(ent_[0-9a-fA-F]{1,16}|self)$")
    private val addressPattern = Regex("^0x[0-9a-fA-F]{1,16}$")
    private val kinds = setOf("player", "ai", "loot", "vehicle", "objective")
    private val diagnosticStatuses = setOf("waiting for gameplay", "game process unavailable",
        "world unavailable", "level unavailable", "actor array unavailable", "world sampled")
    private val actorFields = listOf(
        "x", "y", "z", "yaw", "pitch", "fov", "team", "health", "maxHealth",
        "healthKnown", "dead", "positionPlaintext", "positionDecoded", "renderedOnScreen", "halfHeight",
        "bonesVerified", "boneCount"
    )
    private val positionFields = listOf(
        "boundsX", "boundsY", "boundsZ", "boundsIndex", "boundsEncrypted", "boundsFlags",
        "worldIndex", "worldEncrypted", "worldFlags"
    )

    private fun copyScalars(source: JSONObject, target: JSONObject, fields: List<String>) {
        for (field in fields) {
            val value = source.opt(field)
            if (value is Number || value is Boolean) target.put(field, value)
        }
    }

    private fun address(source: JSONObject, target: JSONObject, key: String) {
        val value = source.optString(key, "")
        if (addressPattern.matches(value)) target.put(key, value)
    }

    fun format(json: JSONObject, session: String, startedAt: Long, receivedAt: Long,
               elapsedMs: Long, dropped: Long): String {
        val result = JSONObject()
            .put("schema", 2).put("type", "snapshot").put("session", session)
            .put("sessionStartedAt", startedAt).put("receivedAt", receivedAt)
            .put("elapsedMs", elapsedMs).put("skippedSamples", dropped)
        val self = json.optString("self", "")
        if (idPattern.matches(self)) result.put("self", self)
        copyScalars(json, result, listOf("cameraSampledAt", "cameraTimestamp",
            "cameraPositionPlaintext", "localPlayerControlled", "entitiesTruncated"))
        json.optJSONObject("diagnostics")?.let { source ->
            val data = JSONObject()
            val status = source.optString("status", "")
            data.put("status", if (status in diagnosticStatuses) status else "unknown")
            for (key in listOf("gworld", "world", "level", "actorArray", "failureAddress"))
                address(source, data, key)
            copyScalars(source, data, listOf("actorCount", "readFailures", "failureErrno"))
            result.put("diagnostics", data)
        }
        val list = json.optJSONArray("entities")
        val actors = JSONArray()
        var omittedLoot = 0
        var omittedOtherEntities = 0
        for (i in 0 until (list?.length() ?: 0)) {
            if (list == null) break
            val source = list.optJSONObject(i) ?: continue
            val kind = source.optString("kind", "")
            if (kind == "loot") {
                omittedLoot++
                continue
            }
            if (actors.length() >= 512) {
                omittedOtherEntities++
                continue
            }
            val actor = JSONObject()
            val id = source.optString("id", "")
            if (idPattern.matches(id)) actor.put("id", id)
            actor.put("kind", if (kind in kinds) kind else "unknown")
            address(source, actor, "classAddress")
            copyScalars(source, actor, actorFields)
            source.optJSONObject("positionTrace")?.let { position ->
                val data = JSONObject()
                address(position, data, "root")
                copyScalars(position, data, positionFields)
                actor.put("positionTrace", data)
            }
            actors.put(actor)
        }
        result.put("entityCount", list?.length() ?: JSONObject.NULL)
            .put("omittedLoot", omittedLoot)
            .put("omittedOtherEntities", omittedOtherEntities)
            .put("entitiesTruncatedInTrace", omittedOtherEntities > 0)
            .put("entities", actors)
        return result.toString()
    }
}

internal class PositionTraceRecorder(
    private val file: PositionTraceFile,
    private val reportError: (String, Throwable) -> Unit
) {
    companion object {
        const val DURATION_MS = 30L * 60 * 1000
        private const val INTERVAL_MS = 1000L
    }

    private val executor = Executors.newSingleThreadExecutor { task ->
        Thread(task, "position-trace").apply { isDaemon = true }
    }
    private var enabled = false
    private var closed = false
    private var pending = false
    private var startedElapsed = 0L
    private var startedWall = 0L
    private var lastSample = -INTERVAL_MS
    private var skipped = 0L
    private var totalSkipped = 0L
    private var session = ""
    @Volatile var status: String = "Off"; private set

    @Synchronized fun isRecording() = enabled

    @Synchronized fun start(wall: Long, elapsed: Long): Boolean {
        if (enabled || closed) return false
        session = UUID.randomUUID().toString()
        startedElapsed = elapsed
        startedWall = wall
        lastSample = elapsed - INTERVAL_MS
        skipped = 0
        totalSkipped = 0
        enabled = true
        status = "Recording; waiting for feed (up to 30 minutes)"
        return true
    }

    @Synchronized fun stop(reason: String = "Stopped") {
        enabled = false
        status = reason
    }

    @Synchronized fun expire(elapsed: Long) {
        if (enabled && elapsed - startedElapsed >= DURATION_MS) stop("Stopped: 30-minute limit reached")
    }

    @Synchronized fun offer(json: JSONObject, wall: Long, elapsed: Long) {
        expire(elapsed)
        if (!enabled || elapsed - lastSample < INTERVAL_MS) return
        lastSample = elapsed
        if (pending) { skipped++; totalSkipped++; return }
        pending = true
        val sampleSession = session
        val sampleStart = startedWall
        val sampleElapsed = elapsed - startedElapsed
        val sampleSkipped = skipped
        skipped = 0
        try {
            executor.execute {
                var written = false
                try {
                    file.append(PositionTraceSnapshot.format(json, sampleSession, sampleStart,
                        wall, sampleElapsed, sampleSkipped))
                    written = true
                } catch (e: IOException) {
                    fail(sampleSession, "Trace file error", e)
                } catch (e: JSONException) {
                    fail(sampleSession, "Trace serialization error", e)
                } catch (e: SecurityException) {
                    fail(sampleSession, "Trace access denied", e)
                } finally {
                    synchronized(this) {
                        pending = false
                        if (written && enabled && session == sampleSession)
                            status = "Recording; samples skipped: $totalSkipped"
                    }
                }
            }
        } catch (e: RejectedExecutionException) {
            pending = false
            fail(sampleSession, "Trace worker unavailable", e)
        }
    }

    @Synchronized private fun fail(failedSession: String, message: String, error: Throwable) {
        if (session == failedSession) {
            enabled = false
            status = "$message: ${error.message ?: error.javaClass.simpleName}"
        }
        reportError(message, error)
    }

    @Synchronized fun close() {
        enabled = false
        closed = true
        executor.shutdown()
    }
}
