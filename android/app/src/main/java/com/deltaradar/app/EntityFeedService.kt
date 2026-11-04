package com.deltaradar.app

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.net.LocalServerSocket
import android.net.LocalSocket
import android.graphics.drawable.Icon
import android.os.Binder
import android.os.IBinder
import android.os.SystemClock
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.system.Os
import android.system.OsConstants
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
    private var cameraServer: LocalServerSocket? = null
    private var cameraClient: LocalSocket? = null
    private var cameraThread: Thread? = null
    @Volatile private var cameraPeerPid: Int? = null
    @Volatile private var cameraReceivedAt = -1L
    private val cameras = CameraSnapshotStore()
    @Volatile var sourcePeerPid: Int? = null
        private set
    @Volatile private var sourcePeerUid: Int? = null
    private val snapshots = EntitySnapshotStore(SystemClock::elapsedRealtime)
    private val statusHandler = Handler(Looper.getMainLooper())
    private var lastNotificationState: ScannerRuntime.State? = null
    // Kept per service so device tests can exercise the real lifecycle with
    // controlled commands, without launching a scanner or reading game data.
    internal var scannerExecutor: (String, CommandRunner.Cancellation) -> CommandRunner.Result = { command, cancellation ->
        RootUtils.run(command, cancellation = cancellation)
    }
    private val statusUpdate = object : Runnable {
        override fun run() {
            if (scannerRequested(this@EntityFeedService)) {
                val state = ScannerRuntime.state
                if (lastNotificationState != state) {
                    lastNotificationState = state
                    getSystemService(NotificationManager::class.java).notify(1, notification(state))
                    OverlayService.instance?.addLog("[Scanner] ${state.message}")
                }
            }
            statusHandler.postDelayed(this, 500)
        }
    }
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
        private const val REQUEST_PREFS = "scanner_runtime"
        private const val REQUEST_KEY = "scanner_requested"
        @Volatile var instance: EntityFeedService? = null; private set

        fun start(ctx: Context) {
            val intent = Intent(ctx, EntityFeedService::class.java)
            ctx.startForegroundService(intent)
        }
        fun startScanner(ctx: Context) = ctx.startForegroundService(
            Intent(ctx, EntityFeedService::class.java).setAction(ACTION_START_SCANNER))
        private fun scannerRequested(ctx: Context) =
            ctx.getSharedPreferences(REQUEST_PREFS, Context.MODE_PRIVATE).getBoolean(REQUEST_KEY, false)
        private fun setScannerRequested(ctx: Context, requested: Boolean) {
            // The system can recreate a sticky service with a null intent.
            // Persist intent before returning from its Start/Stop command.
            check(ctx.getSharedPreferences(REQUEST_PREFS, Context.MODE_PRIVATE)
                .edit().putBoolean(REQUEST_KEY, requested).commit()) { "Could not save scanner request" }
        }
        fun stop(ctx: Context): Boolean {
            setScannerRequested(ctx, false)
            ScannerRuntime.stop()
            return ctx.stopService(Intent(ctx, EntityFeedService::class.java))
        }
    }

    override fun onCreate() {
        super.onCreate()
        instance = this
        getSystemService(NotificationManager::class.java).createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "Delta Radar", NotificationManager.IMPORTANCE_LOW)
        )
        startForeground(1, notification(null))
        statusHandler.post(statusUpdate)
    }

    private fun notification(state: ScannerRuntime.State?): Notification {
        val builder = Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("Delta Radar")
            .setContentText(state?.message ?: "Overlay listener ready")
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .setOnlyAlertOnce(true).setOngoing(true)
        if (state != null) {
            if (state.phase == ScannerRuntime.Phase.FAILED) builder.addAction(
                Notification.Action.Builder(Icon.createWithResource(this, android.R.drawable.ic_menu_rotate), "Retry scanner",
                    PendingIntent.getService(this, 1,
                        Intent(this, EntityFeedService::class.java).setAction(ACTION_START_SCANNER),
                        PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)).build())
            builder.addAction(Notification.Action.Builder(Icon.createWithResource(this, android.R.drawable.ic_menu_close_clear_cancel), "Stop",
                PendingIntent.getService(this, 2,
                    Intent(this, EntityFeedService::class.java).setAction(ACTION_STOP_SCANNER),
                    PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_IMMUTABLE)).build())
        }
        return builder.build()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP_SCANNER) {
            setScannerRequested(this, false)
            ScannerRuntime.stop()
            OverlayService.stop(this)
            stopSelf()
            return START_NOT_STICKY
        }
        if (running.compareAndSet(false, true)) { startListener(); startCameraListener() }
        if (intent?.action == ACTION_START_SCANNER || (intent == null && scannerRequested(this))) {
            setScannerRequested(this, true)
            OverlayService.show(this)
            ScannerRuntime.start(this, scannerExecutor)
        }
        // A fixture/listener-only start must never resurrect a production scan.
        return if (scannerRequested(this)) START_STICKY else START_NOT_STICKY
    }

    override fun onDestroy() {
        statusHandler.removeCallbacksAndMessages(null)
        ScannerRuntime.stopIfOwned(this)
        synchronized(socketLock) {
            running.set(false)
            // close() alone can leave Android's native accept/read holding the
            // descriptor alive. Wake those calls before closing the descriptors.
            try { localServer?.fileDescriptor?.let { Os.shutdown(it, OsConstants.SHUT_RDWR) } }
            catch (_: Exception) {}
            try { cameraServer?.fileDescriptor?.let { Os.shutdown(it, OsConstants.SHUT_RDWR) } }
            catch (_: Exception) {}
            cameraClient?.let(::closeClient)
            try { cameraServer?.close() } catch (_: Exception) {}
            activeClient?.let(::closeClient)
            try { localServer?.close() } catch (_: Exception) {}
        }
        // Bounded shutdown: do not let the next service race an old blocked
        // acceptor for the same abstract socket name.
        try { listenerThread?.join(250) } catch (_: InterruptedException) { Thread.currentThread().interrupt() }
        try { cameraThread?.join(250) } catch (_: InterruptedException) { Thread.currentThread().interrupt() }
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
                    try {
                        val peer = client.peerCredentials
                        sourcePeerPid = peer.pid
                        sourcePeerUid = peer.uid
                        Log.i(TAG, "Entity socket accepted: peerPid=${peer.pid} peerUid=${peer.uid}")
                        client.soTimeout = 2000
                        BoundedLineReader(InputStreamReader(client.inputStream, Charsets.UTF_8),
                            EntitySnapshotStore.MAX_FRAME_CHARS).use { reader ->
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


    private fun startCameraListener() {
        cameraThread = thread(isDaemon = true, name = "camera-feed") {
            var server: LocalServerSocket? = null
            try {
                server = LocalServerSocket("delta_radar_camera")
                val published = synchronized(socketLock) {
                    if (running.get()) { cameraServer = server; true } else false
                }
                if (!published) return@thread
                while (running.get()) {
                    val client = try { server.accept() } catch (_: Exception) { break }
                    val accepted = synchronized(socketLock) {
                        if (running.get()) { cameraClient = client; true } else false
                    }
                    if (!accepted) { closeClient(client); break }
                    try {
                        cameraPeerPid = client.peerCredentials.pid
                        client.soTimeout = 2000
                        BoundedLineReader(InputStreamReader(client.inputStream, Charsets.UTF_8), 4096).use { reader ->
                            while (running.get()) {
                                val line = reader.readLine() ?: break
                                try {
                                    val now = SystemClock.elapsedRealtime()
                                    cameraReceivedAt = cameras.ingest(line, now)
                                } catch (_: Exception) { cameraReceivedAt = -1L; cameras.clear() }
                            }
                        }
                    } catch (e: Exception) {
                        if (running.get()) Log.w(TAG, "Camera client: ${e.message}")
                    } finally {
                        closeClient(client)
                        synchronized(socketLock) {
                            if (cameraClient === client) { cameraClient = null; cameraPeerPid = null; cameraReceivedAt = -1L }
                        }
                        cameras.clear()
                    }
                }
            } catch (e: Exception) {
                if (running.get()) Log.w(TAG, "Camera listener: ${e.message}")
            } finally {
                try { server?.close() } catch (_: Exception) {}
                cameraPeerPid = null
                cameraReceivedAt = -1L
                cameras.clear()
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
    fun clearEntities() { snapshots.clear(); cameras.clear() }

    internal fun hasLiveScannerPeer(pid: Int): Boolean {
        if (!running.get()) return false
        val now = SystemClock.elapsedRealtime()
        return ScannerTransportHealth.isFresh(pid, cameraPeerPid, cameraReceivedAt, now) ||
            ScannerTransportHealth.isFresh(pid, sourcePeerPid, snapshots.status().receivedAtMs, now)
    }

    internal fun getProjectionSnapshot(): EntitySnapshotStore.Snapshot {
        val snapshot = snapshots.projectionSnapshot()
        val world = snapshot.json
        val peer = cameraPeerPid
        if (peer != null) {
            snapshot.cameraRequired = true
            val camera = cameras.snapshot()
            snapshot.cameraSequence = camera.sequence
            if (peer == sourcePeerPid) snapshot.camera = camera.camera
        } else if (world.has("cameraPositionPlaintext")) {
            snapshot.cameraRequired = true
            val list = world.getJSONArray("entities")
            val self = world.optString("self")
            for (i in 0 until list.length()) {
                val e = list.getJSONObject(i)
                if (e.optString("id") == self) snapshot.camera = LiveCamera(
                    world.optInt("gamePid"), world.optString("world"), world.optLong("cameraSampledElapsedMs"),
                    world.optDouble("cameraTimestamp"), world.optBoolean("cameraPositionPlaintext"),
                    e.optDouble("x"), e.optDouble("y"), e.optDouble("z"), e.optDouble("yaw"),
                    e.optDouble("pitch"), world.optDouble("cameraRoll", 0.0), e.optDouble("fov"))
            }
        }
        return snapshot
    }

    private fun log(message: String) {
        OverlayService.instance?.addLog("[Feed] $message")
    }
}
