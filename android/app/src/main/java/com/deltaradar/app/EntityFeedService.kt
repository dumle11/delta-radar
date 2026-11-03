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
import android.os.SystemClock
import android.util.Log
import android.system.Os
import android.system.OsConstants
import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.concurrent.thread

/** Android-only entity transport. There is no HTTP listener or web surface. */
class EntityFeedService : Service() {
    inner class LocalBinder : Binder() { val service: EntityFeedService get() = this@EntityFeedService }
    private val binder = LocalBinder()
    override fun onBind(intent: Intent?): IBinder = binder

    private val running = AtomicBoolean(false)
    private val socketLock = Any()
    private var localServer: LocalServerSocket? = null
    private var activeClient: LocalSocket? = null
    private var listenerThread: Thread? = null
    @Volatile var sourcePeerPid: Int? = null
        private set
    @Volatile private var sourcePeerUid: Int? = null
    private val snapshots = EntitySnapshotStore(SystemClock::elapsedRealtime)
    val feedStatus: EntitySnapshotStore.Status get() = snapshots.status()
    val entityCount: Int get() = feedStatus.entityCount

    @Volatile var isFeedRunning = false
    @Volatile var feedError: String? = null

    companion object {
        private const val TAG = "DeltaRadar"
        private const val CHANNEL_ID = "delta_radar_service"
        private const val SOCKET_NAME = "delta_radar_feed"
        const val ACTION_START_SCANNER = "com.deltaradar.app.START_SCANNER"
        const val ACTION_STOP_SCANNER = "com.deltaradar.app.STOP_SCANNER"
        @Volatile var instance: EntityFeedService? = null; private set

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
        if (intent?.action == ACTION_STOP_SCANNER) {
            ScannerRuntime.stop()
            OverlayService.stop(this)
            stopSelf()
            return START_NOT_STICKY
        }
        if (running.compareAndSet(false, true)) startListener()
        if (intent?.action == ACTION_START_SCANNER) {
            OverlayService.show(this)
            ScannerRuntime.start(this)
        }
        return START_STICKY
    }

    override fun onDestroy() {
        ScannerRuntime.stopIfOwned(this)
        synchronized(socketLock) {
            running.set(false)
            // close() alone can leave Android's native accept/read holding the
            // descriptor alive. Wake those calls before closing the descriptors.
            try { localServer?.fileDescriptor?.let { Os.shutdown(it, OsConstants.SHUT_RDWR) } }
            catch (_: Exception) {}
            activeClient?.let(::closeClient)
            try { localServer?.close() } catch (_: Exception) {}
        }
        // Bounded shutdown: do not let the next service race an old blocked
        // acceptor for the same abstract socket name.
        try { listenerThread?.join(250) } catch (_: InterruptedException) { Thread.currentThread().interrupt() }
        clearEntities()
        isFeedRunning = false
        instance = null
        super.onDestroy()
    }

    private fun closeClient(client: LocalSocket) {
        try { client.shutdownInput() } catch (_: Exception) {}
        try { client.shutdownOutput() } catch (_: Exception) {}
        try { client.close() } catch (_: Exception) {}
    }

    private fun startListener() {
        listenerThread = thread(isDaemon = true, name = "entity-feed") {
            try {
                val server = LocalServerSocket(SOCKET_NAME)
                val published = synchronized(socketLock) {
                    if (running.get()) { localServer = server; true } else false
                }
                if (!published) { server.close(); return@thread }
                isFeedRunning = true
                feedError = null
                log("Local entity feed ready")
                while (running.get()) {
                    val client = try { server.accept() } catch (_: Exception) { break }
                    val accepted = synchronized(socketLock) {
                        if (running.get()) { activeClient = client; true } else false
                    }
                    if (!accepted) { closeClient(client); break }
                    val peer = client.peerCredentials
                    sourcePeerPid = peer.pid
                    sourcePeerUid = peer.uid
                    Log.i(TAG, "Entity socket accepted: peerPid=${peer.pid} peerUid=${peer.uid}")
                    client.soTimeout = 2000
                    try {
                        BufferedReader(InputStreamReader(client.inputStream), 256 * 1024).use { reader ->
                            while (running.get()) {
                                val line = reader.readLine() ?: break
                                if (running.get() && line.startsWith("{")) ingest(line)
                            }
                        }
                    } catch (e: Exception) {
                        if (running.get()) Log.w(TAG, "Entity feed client: ${e.message}")
                    } finally {
                        closeClient(client)
                        synchronized(socketLock) {
                            if (activeClient === client) { activeClient = null; sourcePeerPid = null; sourcePeerUid = null }
                        }
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
                running.set(false)
                clearEntities()
                try { localServer?.close() } catch (_: Exception) {}
            }
        }
    }


    fun ingest(jsonLine: String): Boolean {
        if (!running.get()) return false
        val accepted = snapshots.ingest(jsonLine)
        if (!accepted) Log.w(TAG, "Rejected invalid entity frame")
        else OverlayService.instance?.requestEnemyLinesRefresh()
        CaptureDiagnostics.record(this, "latest-scanner-frame.json") {
            val snapshot = snapshots.snapshot()
            org.json.JSONObject().put("rawLine", jsonLine).put("valid", accepted)
                .put("accepted", snapshot.json).put("sequence", snapshot.status.sequence)
                .put("peerPid", sourcePeerPid).put("peerUid", sourcePeerUid)
        }
        return accepted
    }

    fun getSnapshot(): EntitySnapshotStore.Snapshot = snapshots.snapshot()
    fun isCurrent(status: EntitySnapshotStore.Status): Boolean = running.get() && snapshots.isCurrent(status)
    fun canRender(status: EntitySnapshotStore.Status): Boolean = running.get() && snapshots.canRender(status)
    fun clearEntities() = snapshots.clear()

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
