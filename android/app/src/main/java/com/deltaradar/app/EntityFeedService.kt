package com.deltaradar.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.LocalServerSocket
import android.net.LocalSocket
import android.os.Binder
import android.os.Build
import android.os.IBinder
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.util.Log
import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread
import org.json.JSONArray
import org.json.JSONObject

/** Android-only entity transport. There is no HTTP listener or web surface. */
class EntityFeedService : Service() {
    inner class LocalBinder : Binder() { val service: EntityFeedService get() = this@EntityFeedService }
    private val binder = LocalBinder()
    override fun onBind(intent: Intent?): IBinder = binder

    private val running = AtomicBoolean(false)
    private var localServer: LocalServerSocket? = null
    private var activeClient: LocalSocket? = null
    private var cameraServer: LocalServerSocket? = null
    private var cameraClient: LocalSocket? = null
    @Volatile private var cameraConnected = false
    @Volatile private var liveCamera: LiveCamera? = null
    @Volatile private var cameraPackets = 0L
    @Volatile private var cameraFrames = 0L
    @Volatile internal var projectedFrames = 0L
    private var nativeGamePid = 0
    private var nativeWorld = ""
    @Volatile private var publishedWorld = JSONObject()
    private val entities = ConcurrentHashMap<String, JSONObject>()
    private val missingFrames = ConcurrentHashMap<String, Int>()
    private var diagnosticData: JSONObject? = null
    private var diagnosticEntities: JSONArray? = null
    private var diagnosticReceivedAt = 0L
    private var frameSequence = 0L
    private lateinit var positionTrace: PositionTraceRecorder
    private val traceHandler = Handler(Looper.getMainLooper())
    private val traceDeadline = Runnable {
        positionTrace.expire(SystemClock.elapsedRealtime())
    }

    @Volatile var selfId: String? = null
    @Volatile var lastUpdateTs = 0L
    @Volatile var cameraSampledAt = 0L; private set
    @Volatile var cameraTimestamp = -1.0; private set
    @Volatile var cameraPositionPlaintext = false; private set
    @Volatile var localPlayerControlled = false; private set
    @Volatile var entityCount = 0
    @Volatile var isFeedRunning = false
    @Volatile var feedError: String? = null

    companion object {
        private const val TAG = "DeltaRadar"
        private const val CHANNEL_ID = "delta_radar_service"
        private const val SOCKET_NAME = "delta_radar_feed"
        var instance: EntityFeedService? = null; private set

        fun start(ctx: Context) {
            val intent = Intent(ctx, EntityFeedService::class.java)
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) ctx.startForegroundService(intent)
            else ctx.startService(intent)
        }
        fun stop(ctx: Context) = ctx.stopService(Intent(ctx, EntityFeedService::class.java))
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        positionTrace = PositionTraceRecorder(PositionTraceFile(filesDir)) { message, error ->
            Log.e(TAG, message, error)
            log("$message: ${error.message ?: error.javaClass.simpleName}")
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
            getSystemService(NotificationManager::class.java).createNotificationChannel(
                NotificationChannel(CHANNEL_ID, "Delta Radar", NotificationManager.IMPORTANCE_LOW)
            )
        }
        startForeground(1, Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("Delta Radar")
            .setContentText("Overlay entity feed active")
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .build())
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (running.compareAndSet(false, true)) { startListener(); startCameraListener() }
        return START_STICKY
    }

    override fun onDestroy() {
        running.set(false)
        traceHandler.removeCallbacks(traceDeadline)
        positionTrace.close()
        try { cameraClient?.close() } catch (_: Exception) {}
        try { cameraServer?.close() } catch (_: Exception) {}
        try { activeClient?.close() } catch (_: Exception) {}
        try { localServer?.close() } catch (_: Exception) {}
        clearEntities()
        instance = null
        super.onDestroy()
    }

    private fun startListener() = thread(isDaemon = true, name = "entity-feed") {
        try {
            val server = LocalServerSocket(SOCKET_NAME)
            localServer = server
            isFeedRunning = true
            feedError = null
            log("Local entity feed ready")
            while (running.get()) {
                val client = try { server.accept() } catch (_: Exception) { break }
                activeClient = client
                try {
                    BufferedReader(InputStreamReader(client.inputStream), 256 * 1024).use { reader ->
                        while (running.get()) {
                            val line = reader.readLine() ?: break
                            if (line.startsWith("{")) ingest(line)
                        }
                    }
                } catch (e: Exception) {
                    if (running.get()) Log.w(TAG, "Entity feed client: ${e.message}")
                } finally {
                    try { client.close() } catch (_: Exception) {}
                    activeClient = null
                    // A disconnected scanner cannot provide current camera or
                    // actor state. Never leave its last match painted over a
                    // loading screen, menu, or restarted game process.
                    clearEntities()
                }
            }
        } catch (e: Exception) {
            feedError = e.message
            Log.e(TAG, "Entity feed failed", e)
        } finally {
            isFeedRunning = false
        }
    }

    private fun startCameraListener() = thread(isDaemon = true, name = "camera-feed") {
        try {
            val server = LocalServerSocket("delta_radar_camera")
            cameraServer = server
            while (running.get()) {
                val client = try { server.accept() } catch (_: Exception) { break }
                cameraClient = client; cameraConnected = true
                try {
                    BufferedReader(InputStreamReader(client.inputStream), 8192).use { input ->
                        while (running.get()) {
                            val line = input.readLine() ?: break
                            try {
                                val next = LiveCamera.parse(JSONObject(line))
                                if (next.timestamp != liveCamera?.timestamp || next.world != liveCamera?.world) cameraFrames++
                                liveCamera = next; cameraPackets++
                            }
                            catch (_: Exception) { liveCamera = null }
                        }
                    }
                } catch (_: Exception) { /* The native sampler reconnects. */ }
                finally { cameraConnected = false; liveCamera = null; client.close() }
            }
        } catch (e: Exception) { Log.w(TAG, "Camera feed: ${e.message}") }
        finally { cameraConnected = false; liveCamera = null }
    }

    @Synchronized fun ingest(jsonLine: String): Int {
        return try {
        val json = JSONObject(jsonLine)
        nativeGamePid = json.optInt("gamePid", 0)
        nativeWorld = json.optJSONObject("diagnostics")?.optString("world", "") ?: ""
        val nextSelf = json.optString("self", "").ifEmpty { null }
        val receivedAt = System.currentTimeMillis()
        val list = json.optJSONArray("entities")
        json.optJSONObject("diagnostics")?.let {
            diagnosticData = it
            diagnosticEntities = list
            diagnosticReceivedAt = receivedAt
        }
        positionTrace.offer(json, receivedAt, SystemClock.elapsedRealtime())
        if (list == null) return 0
        frameSequence++
        val seen = HashSet<String>(list.length())
        var accepted = 0

        for (i in 0 until list.length()) {
            val raw = list.optJSONObject(i) ?: continue
            val id = raw.optString("id", "")
            if (id.isEmpty()) continue
            val health = raw.optDouble("health", 100.0)
            val healthKnown = raw.optBoolean("healthKnown", false)
            val dead = raw.optBoolean("dead", false) || (healthKnown && health <= 0.01)
            val isSelf = id == nextSelf
            if (dead && !isSelf) {
                entities.remove(id)
                missingFrames.remove(id)
                continue
            }

            val e = JSONObject()
            for (key in listOf(
                "id", "kind", "name", "team", "x", "y", "z", "yaw", "pitch", "fov",
                "health", "maxHealth", "healthKnown", "halfHeight", "dead", "bones",
                "positionPlaintext", "positionDecoded", "renderedOnScreen",
                "bonesVerified", "boneCount", "boneParents", "sampledAt"
            )) if (raw.has(key) && !raw.isNull(key)) e.put(key, raw.get(key))
            if (!e.has("kind")) e.put("kind", "player")
            if (!e.has("team")) e.put("team", 0)
            if (!e.has("maxHealth")) e.put("maxHealth", 100.0)
            if (!e.has("halfHeight")) e.put("halfHeight", 1.05)
            val old = entities[id]
            if (old != null && old.optString("name") == e.optString("name")) {
                val v = WorldMotion.velocity(e.optDouble("x"), e.optDouble("y"), e.optDouble("z"), e.optLong("sampledAt"),
                    old.optDouble("x"), old.optDouble("y"), old.optDouble("z"), old.optLong("sampledAt"))
                e.put("velocityX",v.x).put("velocityY",v.y).put("velocityZ",v.z)
            }
            e.put("lastSeen", receivedAt)
            e.put("seenFrame", frameSequence)
            entities[id] = e
            missingFrames.remove(id)
            seen.add(id)
            // Keep a dead local pawn as the projection/camera anchor while
            // spectating. OverlayService never draws self, so this does not
            // resurrect its box; it only prevents the whole frame from being
            // discarded after death.
            if (!dead) accepted++
        }

        // Explicit death is immediate. A temporarily missing streamed-level
        // pawn gets a short grace window so app focus/screenshot transitions do
        // not make human boxes vanish while Unreal rebuilds its level arrays.
        entities.keys.toList().forEach { id ->
            if (id !in seen) {
                val misses = (missingFrames[id] ?: 0) + 1
                if (misses >= 12) {
                    entities.remove(id)
                    missingFrames.remove(id)
                } else missingFrames[id] = misses
            }
        }
        cameraSampledAt = json.optLong("cameraSampledAt", 0L)
        cameraTimestamp = json.optDouble("cameraTimestamp", -1.0).takeIf { it.isFinite() } ?: -1.0
        cameraPositionPlaintext = json.optBoolean("cameraPositionPlaintext", false)
        localPlayerControlled = json.optBoolean("localPlayerControlled", false)
        selfId = nextSelf?.takeIf { it in seen }
        lastUpdateTs = receivedAt
        entityCount = entities.size
        val array = JSONArray()
        entities.values.forEach { array.put(it) }
        val published = JSONObject().put("entities",array).put("ts",lastUpdateTs)
            .put("frameSequence",frameSequence).put("gamePid",nativeGamePid).put("world",nativeWorld)
            .put("cameraSampledAt",cameraSampledAt).put("cameraTimestamp",cameraTimestamp)
            .put("cameraPositionPlaintext",cameraPositionPlaintext).put("localPlayerControlled",localPlayerControlled)
        selfId?.let { published.put("self",it) }
        publishedWorld = published
        accepted
        } catch (e: Exception) {
            Log.w(TAG, "Entity ingest: ${e.message}")
            0
        }
    }

    fun getSnapshot(): JSONObject {
        val world = publishedWorld
        val result = JSONObject()
        world.keys().forEach { key -> result.put(key,world.get(key)) }
        result.put("cameraPackets",cameraPackets).put("cameraFrames",cameraFrames).put("projectedFrames",projectedFrames)
        if (cameraConnected) {
            val c = liveCamera
            val usable = c?.usable(world.optInt("gamePid"),world.optString("world"),System.currentTimeMillis()) == true
            result.put("cameraPositionPlaintext",usable)
            result.put("cameraSampledAt",c?.sampledAt ?: 0).put("cameraTimestamp",c?.timestamp ?: -1.0)
            if (usable) result.put("camera",c!!.json())
        }
        result.put("snapshotAt",System.currentTimeMillis())
        return result
    }

    internal fun positionTraceStatus(): String {
        positionTrace.expire(SystemClock.elapsedRealtime())
        return positionTrace.status
    }
    internal fun isPositionTraceRecording(): Boolean {
        positionTrace.expire(SystemClock.elapsedRealtime())
        return positionTrace.isRecording()
    }
    internal fun startPositionTrace() {
        if (positionTrace.start(System.currentTimeMillis(), SystemClock.elapsedRealtime())) {
            traceHandler.removeCallbacks(traceDeadline)
            traceHandler.postDelayed(traceDeadline, PositionTraceRecorder.DURATION_MS)
            log("Position trace started; recording existing scanner snapshots only")
        }
    }
    internal fun stopPositionTrace() {
        traceHandler.removeCallbacks(traceDeadline)
        positionTrace.stop()
        log("Position trace stopped")
    }

    @Synchronized fun clearEntities() {
        cameraSampledAt = 0L
        cameraTimestamp = -1.0
        cameraPositionPlaintext = false
        localPlayerControlled = false
        entities.clear()
        publishedWorld = JSONObject()
        missingFrames.clear()
        selfId = null
        lastUpdateTs = 0L
        entityCount = 0
        diagnosticData = null
        diagnosticEntities = null
        diagnosticReceivedAt = 0L
        frameSequence = 0L
    }

    @Synchronized internal fun getDiagnosticReport(): DiagnosticReport? {
        val data = diagnosticData ?: return null
        val list = diagnosticEntities
        val total = list?.length() ?: -1
        val actors = ArrayList<DiagnosticActor>(minOf(total.coerceAtLeast(0), DiagnosticTable.MAX_ROWS))
        for (i in 0 until minOf(total, DiagnosticTable.MAX_ROWS)) {
            val actor = list?.optJSONObject(i) ?: continue
            val id = actor.optString("id", "")
            val address = if (id.startsWith("ent_")) "0x${id.removePrefix("ent_")}" else "synthetic"
            val health = if (actor.optBoolean("healthKnown", false))
                "${actor.optDouble("health")}/${actor.optDouble("maxHealth")}" else "unknown"
            val status = if (!actor.has("dead")) "unknown" else if (actor.optBoolean("dead")) "dead" else "alive*"
            val kind = actor.optString("kind", "unknown")
            val position = when {
                kind == "loot" -> "plaintext transform"
                actor.optBoolean("positionDecoded", false) -> "decoded bounds"
                actor.optBoolean("positionPlaintext", false) -> "checks passed"
                else -> "unverified"
            }
            actors.add(DiagnosticActor(address, kind, actor.optString("name", "unknown"),
                actor.optString("classAddress", "unknown").takeUnless { it == "0x0" } ?: "unknown",
                actor.optString("className", "").ifEmpty { "unknown" }, health, status, position))
        }
        fun address(key: String) = data.optString(key, "unknown").takeUnless { it == "0x0" } ?: "unavailable"
        return DiagnosticReport(diagnosticReceivedAt, data.optString("status", "unknown"),
            address("gworld"), address("world"), address("level"), address("actorArray"),
            if (data.has("actorCount")) data.optInt("actorCount").toString() else "unknown",
            data.optInt("readFailures"), address("failureAddress"), data.optInt("failureErrno"),
            actors, total)
    }

    override fun dump(fd: java.io.FileDescriptor, writer: java.io.PrintWriter, args: Array<out String>) {
        writer.println(getSnapshot().toString())
    }

    private val logLines = mutableListOf<String>()
    fun getLogLines(): List<String> = synchronized(logLines) { logLines.toList() }
    fun log(message: String) {
        synchronized(logLines) {
            logLines.add(message)
            if (logLines.size > 100) logLines.removeAt(0)
        }
        OverlayService.instance?.addLog("[Feed] $message")
    }
}
