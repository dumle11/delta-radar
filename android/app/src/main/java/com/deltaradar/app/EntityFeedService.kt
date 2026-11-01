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
    private val entities = ConcurrentHashMap<String, JSONObject>()
    private val missingFrames = ConcurrentHashMap<String, Int>()

    @Volatile var selfId: String? = null
    @Volatile var lastUpdateTs = 0L
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
        if (running.compareAndSet(false, true)) startListener()
        return START_STICKY
    }

    override fun onDestroy() {
        running.set(false)
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

    fun ingest(jsonLine: String): Int = try {
        val json = JSONObject(jsonLine)
        val nextSelf = json.optString("self", "").ifEmpty { null }
        val list = json.optJSONArray("entities") ?: return 0
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
                "health", "maxHealth", "healthKnown", "halfHeight", "dead", "bones"
            )) if (raw.has(key) && !raw.isNull(key)) e.put(key, raw.get(key))
            if (!e.has("kind")) e.put("kind", "player")
            if (!e.has("team")) e.put("team", 0)
            if (!e.has("maxHealth")) e.put("maxHealth", 100.0)
            if (!e.has("halfHeight")) e.put("halfHeight", 1.05)
            e.put("lastSeen", System.currentTimeMillis())
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
        selfId = nextSelf?.takeIf { it in seen }
        lastUpdateTs = System.currentTimeMillis()
        entityCount = entities.size
        accepted
    } catch (e: Exception) {
        Log.w(TAG, "Entity ingest: ${e.message}")
        0
    }

    fun getSnapshot(): JSONObject {
        val result = JSONObject()
        selfId?.let { result.put("self", it) }
        val array = JSONArray()
        entities.values.forEach { array.put(JSONObject(it.toString())) }
        result.put("entities", array)
        result.put("ts", lastUpdateTs)
        return result
    }

    fun clearEntities() {
        entities.clear()
        missingFrames.clear()
        selfId = null
        lastUpdateTs = 0L
        entityCount = 0
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
