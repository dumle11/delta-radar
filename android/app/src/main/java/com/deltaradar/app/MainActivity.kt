package com.deltaradar.app

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import java.util.concurrent.atomic.AtomicBoolean
import android.provider.Settings
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import kotlin.concurrent.thread

class MainActivity : AppCompatActivity() {

    private val handler = Handler(Looper.getMainLooper())
    private var entityFeed: EntityFeedService? = null
    private var bound = false
    private var statusRunnable: Runnable? = null
    private val starting get() = ScannerRuntime.state.phase == ScannerRuntime.Phase.STARTING
    private var startupSession: ScannerSession? = null
    private var binding = false
    private var pendingOverlayPermission = false
    private val statusQueryInFlight = AtomicBoolean(false)
    private var rootStatusRequested = false


    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            entityFeed = (binder as EntityFeedService.LocalBinder).service
            bound = true; binding = false
            updateUi()
        }
        override fun onServiceDisconnected(name: ComponentName?) {
            entityFeed = null; bound = false; binding = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.btnStart).setOnClickListener { toggleRadar() }

    }

    override fun onStart() {
        super.onStart()
        rootStatusRequested = false
        tryBindService()
        startStatusLoop()
    }

    override fun onStop() {
        super.onStop()
        stopStatusLoop()
        if (binding || bound) { try { unbindService(connection) } catch (_: Exception) {}; bound = false; binding = false; entityFeed = null }
    }

    override fun onResume() {
        super.onResume()
        if (pendingOverlayPermission && Settings.canDrawOverlays(this)) {
            pendingOverlayPermission = false
            toggleRadar()
        }
    }

    private fun tryBindService() {
        if (bound || binding) return
        try { binding = bindService(Intent(this, EntityFeedService::class.java), connection, 0) }
        catch (_: Exception) { binding = false }
    }

    override fun onDestroy() {
        if (ScannerRuntime.cancelPending(startupSession)) {
            EntityFeedService.stop(this)
            OverlayService.stop(this)
        }
        handler.removeCallbacksAndMessages(null)
        super.onDestroy()
    }

    private fun startStatusLoop() {
        val r = object : Runnable {
            override fun run() { tryBindService(); refreshStatus(); handler.postDelayed(this, 250) }
        }
        statusRunnable = r; r.run()
    }

    private fun stopStatusLoop() {
        statusRunnable?.let { handler.removeCallbacks(it) }; statusRunnable = null
    }

    private fun refreshStatus() {
        // Feed health must not wait for su or inherit its cached PID state.
        val feedOk = entityFeed?.isFeedRunning == true
        val feedStatus = entityFeed?.feedStatus
        val connected = feedOk && feedStatus?.hasCamera == true
        findViewById<TextView>(R.id.txtFeed).apply {
            text = when {
                connected -> "Overlay feed: connected (fresh frame)"
                ScannerRuntime.state.phase == ScannerRuntime.Phase.FAILED -> "Scanner: ${ScannerRuntime.state.message}"
                !feedOk -> "Overlay feed: stopped"
                feedStatus?.fresh == true -> "Overlay feed: idle (no active camera)"
                (feedStatus?.receivedAtMs ?: -1) >= 0 -> "Overlay feed: stale"
                else -> "Overlay feed: waiting for valid frame"
            }
            setTextColor(if (connected) 0xFF4fd08a.toInt() else 0xFF5c6b78.toInt())
        }
        findViewById<TextView>(R.id.txtEntities).text = "Entities: ${feedStatus?.entityCount ?: 0}"
        findViewById<TextView>(R.id.btnStart).apply {
            text = if (starting) "Cancel Start" else if (feedOk || ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING) "Stop Overlay" else "Start Overlay"
            isEnabled = ScannerRuntime.state.phase != ScannerRuntime.Phase.STOPPING
        }
        if (rootStatusRequested || !statusQueryInFlight.compareAndSet(false, true)) return
        rootStatusRequested = true
        thread(name = "root-status") {
            try {
                val status = RootUtils.getFullStatus()
                handler.post {
                    if (isDestroyed) return@post
                    findViewById<TextView>(R.id.txtRoot).apply {
                        text = "Root: ${if (status.hasRoot) "granted" else "not available"}"
                        setTextColor(if (status.hasRoot) 0xFF4fd08a.toInt() else 0xFFe5534b.toInt())
                    }
                    findViewById<TextView>(R.id.txtGame).apply {
                        text = "Delta Force: ${if (status.gamePid != null) "running (PID ${status.gamePid})" else "not running"}"
                        setTextColor(if (status.gamePid != null) 0xFF4fd08a.toInt() else 0xFF5c6b78.toInt())
                    }
                }
            } finally { statusQueryInFlight.set(false) }
        }
    }

    private fun updateUi() { refreshStatus() }

    private fun toggleRadar() {
        if (starting || EntityFeedService.instance?.isFeedRunning == true || ScannerRuntime.state.phase == ScannerRuntime.Phase.RUNNING) {
            stopRadar(); return
        }
        if (!Settings.canDrawOverlays(this)) {
            pendingOverlayPermission = true
            startActivity(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName")))
            return
        }
        EntityFeedService.start(this)
        OverlayService.show(this)
        startupSession = ScannerRuntime.start(this)
        // Start never launches/restarts the game. The user controls gameplay.
        tryBindService()
        refreshStatus()
    }

    private fun stopRadar() {
        if (binding || bound) { try { unbindService(connection) } catch (_: Exception) {}; bound = false; binding = false; entityFeed = null }
        ScannerRuntime.stop()
        EntityFeedService.stop(this)
        OverlayService.stop(this)
        toast("Radar stopped")
        refreshStatus()
    }

    private fun toast(msg: String) { Toast.makeText(this, msg, Toast.LENGTH_SHORT).show() }
}
