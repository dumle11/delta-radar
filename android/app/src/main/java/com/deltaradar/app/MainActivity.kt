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
    private var starting = false
    private var pendingOverlayPermission = false

    private val DFM_PKG = "com.proxima.dfm"

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            entityFeed = (binder as EntityFeedService.LocalBinder).service
            bound = true
            updateUi()
        }
        override fun onServiceDisconnected(name: ComponentName?) {
            entityFeed = null; bound = false
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        findViewById<TextView>(R.id.btnStart).setOnClickListener { toggleRadar() }
        if (Settings.canDrawOverlays(this) && entityFeed?.isFeedRunning != true) {
            handler.postDelayed({ if (!starting && entityFeed?.isFeedRunning != true) toggleRadar() }, 500)
        }
    }

    override fun onStart() {
        super.onStart()
        tryBindService()
        startStatusLoop()
    }

    override fun onStop() {
        super.onStop()
        stopStatusLoop()
        if (bound) { try { unbindService(connection) } catch (_: Exception) {}; bound = false }
    }

    override fun onResume() {
        super.onResume()
        if (pendingOverlayPermission && Settings.canDrawOverlays(this)) {
            pendingOverlayPermission = false
            toggleRadar()
        }
    }

    private fun tryBindService() {
        try { bindService(Intent(this, EntityFeedService::class.java), connection, 0) } catch (_: Exception) {}
    }

    private fun startStatusLoop() {
        val r = object : Runnable {
            override fun run() { refreshStatus(); handler.postDelayed(this, 3000) }
        }
        statusRunnable = r; r.run()
    }

    private fun stopStatusLoop() {
        statusRunnable?.let { handler.removeCallbacks(it) }; statusRunnable = null
    }

    private fun refreshStatus() {
        thread {
            val status = RootUtils.getFullStatus()
            val feedOk = entityFeed?.isFeedRunning == true
            val entityCount = entityFeed?.entityCount ?: 0

            handler.post {
                findViewById<TextView>(R.id.txtRoot).apply {
                    text = "Root: ${if (status.hasRoot) "granted" else "not available"}"
                    setTextColor(if (status.hasRoot) 0xFF4fd08a.toInt() else 0xFFe5534b.toInt())
                }
                findViewById<TextView>(R.id.txtGame).apply {
                    text = "Delta Force: ${if (status.gamePid != null) "running (PID ${status.gamePid})" else "not running"}"
                    setTextColor(if (status.gamePid != null) 0xFF4fd08a.toInt() else 0xFF5c6b78.toInt())
                }
                findViewById<TextView>(R.id.txtFeed).apply {
                    val scannerOk = status.scannerPid != null
                    text = when {
                        feedOk && scannerOk -> "Overlay feed: connected (scanner PID ${status.scannerPid})"
                        feedOk -> "Overlay feed: waiting for scanner"
                        else -> "Overlay feed: stopped"
                    }
                    setTextColor(if (feedOk && scannerOk) 0xFF4fd08a.toInt() else 0xFF5c6b78.toInt())
                }
                findViewById<TextView>(R.id.txtEntities).apply {
                    text = "Entities: $entityCount"
                    setTextColor(0xFF5c6b78.toInt())
                }
                findViewById<TextView>(R.id.btnStart).apply {
                    text = if (starting) "Starting..." else if (feedOk) "Stop Overlay" else "Start Overlay"
                    isEnabled = !starting
                }
            }
        }
    }

    private fun updateUi() { refreshStatus() }

    private fun toggleRadar() {
        if (starting) return
        if (entityFeed?.isFeedRunning == true) {
            stopRadar(); return
        }
        if (!Settings.canDrawOverlays(this)) {
            pendingOverlayPermission = true
            startActivity(Intent(Settings.ACTION_MANAGE_OVERLAY_PERMISSION, Uri.parse("package:$packageName")))
            return
        }
        starting = true
        findViewById<TextView>(R.id.btnStart).apply { text = "Starting..."; isEnabled = false }

        EntityFeedService.start(this)
        OverlayService.show(this)

        val game = packageManager.getLaunchIntentForPackage(DFM_PKG)
        if (game != null) startActivity(game)
        else toast("Delta Force is not installed")

        // The socket listener and native scanner are separate processes. The
        // old launcher only started the listener, leaving the UI at 0 entities
        // whenever memscan had exited. Start it once the game process exists;
        // the scanner itself waits for a valid match/world before publishing.
        thread(name = "scanner-launch") {
            repeat(10) {
                val result = RootUtils.exec(
                    "if pidof $DFM_PKG >/dev/null 2>&1; then " +
                        "if ! pidof memscan >/dev/null 2>&1; then " +
                        "nohup /data/local/tmp/memscan run vmreadv " +
                        ">/data/local/tmp/memscan.log 2>&1 </dev/null & " +
                        "fi; echo READY; fi"
                )
                if (result.contains("READY")) return@thread
                Thread.sleep(750)
            }
        }

        handler.postDelayed({
            tryBindService()
            starting = false
            refreshStatus()
        }, 2000)
    }

    private fun stopRadar() {
        if (bound) { try { unbindService(connection) } catch (_: Exception) {}; bound = false; entityFeed = null }
        EntityFeedService.stop(this)
        OverlayService.stop(this)
        thread(name = "scanner-stop") { RootUtils.exec("killall memscan 2>/dev/null || true") }
        toast("Radar stopped")
        handler.postDelayed(::refreshStatus, 1000)
    }

    private fun toast(msg: String) { Toast.makeText(this, msg, Toast.LENGTH_SHORT).show() }
}
