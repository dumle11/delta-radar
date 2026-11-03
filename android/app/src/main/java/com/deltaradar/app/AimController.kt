package com.deltaradar.app

import android.content.Context
import android.os.SystemClock
import android.util.Log
import java.io.BufferedReader
import java.io.InputStreamReader
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** Bounded touch gestures; never writes game memory or queues old targets. */
class AimController(private val context: Context) : AutoCloseable {
    data class Frame(val targets: List<AimMath.Target>, val width: Int, val height: Int,
                     val sourceTimeMs: Long, val cameraTime: Double,
                     val yaw: Double, val pitch: Double, val fov: Double, val poseTimeMs: Long)
    @Volatile var frame: Frame? = null
    @Volatile var paused = true
    @Volatile var gameForeground = false; private set
    @Volatile var status = "Aim disabled"; private set
    @Volatile var gesturesSent = 0; private set
    @Volatile var shotsSent = 0; private set
    @Volatile var correctionsConfirmed = 0; private set
    private val executor = Executors.newSingleThreadScheduledExecutor { task ->
        Thread(task, "delta-aim").apply { isDaemon = true }
    }
    private val touch = RootTouchInput()
    private var targetId: String? = null
    private var alignedSince = 0L
    private var feedback: AimFeedback? = null
    private var feedbackAt = 0L
    private var feedbackSentAtMs = 0L
    private var inputVerified = false
    private var correctionDegrees = 0.0
    private var correctionCount = 0
    private var yieldUntil = 0L
    @Volatile private var fault: String? = null
    fun faultMessage(): String? = fault
    @Volatile private var resetRequested = false
    fun resetFeedback() { resetRequested = true }
    private var lastShot = 0L
    private var lastFireFinishedAt = 0L
    private var lastShotCameraTime = -1.0
    private var lastFocusCheck = 0L
    @Volatile private var closed = false

    init { executor.scheduleWithFixedDelay({ tickSafely() }, 0, 80, TimeUnit.MILLISECONDS) }

    private fun tickSafely() {
        try { tick() } catch (e: Exception) {
            status = "Touch input unavailable"
            gameForeground = false
            touch.close()
            Log.w("DeltaRadar", "Aim input: ${e.message}")
        }
    }

    private fun tick() {
        if (closed) return
        if (resetRequested) {
            resetRequested = false; feedback = null; targetId = null; fault = null; correctionCount = 0; inputVerified = false; correctionDegrees = 0.0
        }
        val aim = Settings.aimEnabled(context)
        val shoot = Settings.autoShoot(context)
        val now = SystemClock.elapsedRealtime()
        if (!aim && !shoot) { feedback = null; fault = null; correctionCount = 0; inputVerified = false; correctionDegrees = 0.0 }
        if (now - lastFocusCheck >= 500 && (aim || shoot || Settings.aimCircle(context))) {
            lastFocusCheck = now
            gameForeground = touch.foreground()
        }
        val current = frame
        fun usable(): Boolean {
            val latest = frame ?: return false
            return !closed && !paused && gameForeground && current != null &&
                latest.width == current.width && latest.height == current.height &&
                latest.cameraTime >= current.cameraTime &&
                kotlin.math.abs(latest.fov - current.fov) <= 1.0 &&
                System.currentTimeMillis() - current.poseTimeMs in 0..300
        }
        if ((!aim && !shoot) || !usable()) {
            targetId = null; alignedSince = 0
            if ((!aim && !shoot) || paused || !gameForeground) feedback = null
            else if (feedback != null && now - feedbackAt > 1200) {
                feedback = null
                fault = "Aim stopped: correction feedback was lost. Reset aim."
            }
            status = when {
                fault != null && (aim || shoot) -> fault!!
                !aim && !shoot -> "Aim disabled"
                paused -> "Paused while menu is open"
                !gameForeground -> "Waiting for game"
                else -> "Waiting for live player and camera"
            }
            return
        }
        current ?: return
        if (fault != null) { status = fault!!; return }
        // Firing moves the camera independently of our look gesture. Do not
        // send/measure a correction while that recoil is still settling.
        if (now - lastFireFinishedAt in 0..500) {
            status = "Waiting for firing recoil to settle"
            return
        }
        if (now < yieldUntil) { status = "Paused for manual camera movement or zoom"; return }
        feedback?.let { pending ->
            // Only a camera sample captured after this gesture can acknowledge
            // it. Packet arrival time alone does not establish capture time.
            if (current.poseTimeMs <= feedbackSentAtMs + 50) {
                if (now - feedbackAt < 1200) { status = "Waiting for post-gesture camera sample"; return }
                feedback = null
                fault = "Aim stopped: no post-gesture camera sample. Reset aim."
                status = fault!!
                return
            }
            when (pending.observe(current.cameraTime, current.yaw, current.pitch, current.fov,
                    pending.errorPixels, now - feedbackAt)) {
                AimFeedback.Result.WAIT -> { status = "Waiting for camera response"; return }
                AimFeedback.Result.STOP -> {
                    feedback = null
                    fault = "Aim stopped: camera did not converge. Reset aim or set look area."
                    status = fault!!
                    return
                }
                AimFeedback.Result.YIELD -> {
                    feedback = null; targetId = null; alignedSince = 0
                    inputVerified = false
                    yieldUntil = now + 350
                    status = "Paused for manual camera movement or zoom"
                    return
                }
                AimFeedback.Result.IMPROVED -> {
                    feedback = null; correctionsConfirmed++; inputVerified = true
                    correctionDegrees += pending.movementDegrees(current.yaw, current.pitch)
                    if (correctionDegrees > 30.0) {
                        fault = "Aim stopped: correction limit reached. Reset aim."
                        status = fault!!
                        return
                    }
                }
            }
        }
        val target = AimMath.select(current.targets, current.width, current.height,
            Settings.aimFov(context), targetId)
        if (target == null) {
            targetId = null; alignedSince = 0; correctionCount = 0
            status = "No verified, rendered enemy in FOV"
            return
        }
        if (targetId != target.id) { alignedSince = 0; correctionCount = 0; correctionDegrees = 0.0 }
        targetId = target.id
        val firePoint = Settings.aimFirePoint(context)
        status = if (shoot && firePoint == null) "Set the fire button to enable auto shoot" else "Tracking enemy body"
        if (AimMath.crosshairOnBody(target)) {
            correctionDegrees = 0.0; correctionCount = 0
            if (alignedSince == 0L) alignedSince = now
            if (shoot && firePoint != null && now - alignedSince >= 100 && now - lastShot >= 200 && current.cameraTime > lastShotCameraTime) {
                if (touch.tap((firePoint.first * current.width).toInt(),
                        (firePoint.second * current.height).toInt(), ::usable)) {
                    lastShot = now; lastShotCameraTime = current.cameraTime
                    shotsSent++
                    lastFireFinishedAt = SystemClock.elapsedRealtime()
                    feedback = null
                    inputVerified = false
                    status = "Waiting for firing recoil to settle"
                    return
                }
            }
        } else alignedSince = 0
        if (aim && !AimMath.crosshairOnBody(target)) {
            if (correctionCount >= 80) {
                fault = "Aim stopped: target did not converge. Reset aim."
                status = fault!!
                return
            }
            val (dx, dy) = AimMath.correction(target, current.width, current.height,
                Settings.aimStrength(context), kotlin.math.min(current.width, current.height) *
                    if (inputVerified) .012f else .004f)
            if (dx != 0 || dy != 0) {
                val anchor = Settings.aimLookPoint(context)
                val x = (anchor.first * current.width).toInt().coerceIn(1, current.width - 2)
                val y = (anchor.second * current.height).toInt().coerceIn(1, current.height - 2)
                if (touch.swipe(x, y, (x + dx).coerceIn(1, current.width - 2),
                        (y + dy).coerceIn(1, current.height - 2), ::usable)) {
                    feedback = AimFeedback.capture(current.cameraTime, current.yaw, current.pitch,
                        current.fov, target, current.width, current.height)
                    feedbackSentAtMs = System.currentTimeMillis()
                    feedbackAt = SystemClock.elapsedRealtime()
                    correctionCount++
                    gesturesSent++
                }
            }
        }
    }

    override fun close() {
        closed = true; paused = true; frame = null
        touch.close()
        executor.shutdownNow()
    }
}

private class RootTouchInput : AutoCloseable {
    @Volatile private var process: Process? = null
    private var reader: BufferedReader? = null
    private val focus = "dumpsys window displays | grep 'mCurrentFocus=.*com.proxima.dfm/'"

    private fun exchange(command: String, valid: () -> Boolean = { true }): String {
        var proc = process
        if (proc == null || !proc.isAlive) {
            proc = ProcessBuilder("su").redirectErrorStream(true).start()
            process = proc
            reader = BufferedReader(InputStreamReader(proc.inputStream))
        }
        if (!valid()) return ""
        val end = "DELTA_INPUT_${System.nanoTime()}"
        proc.outputStream.write("$command\necho $end\n".toByteArray(Charsets.UTF_8))
        proc.outputStream.flush()
        val result = StringBuilder()
        while (true) {
            val line = reader?.readLine() ?: throw IllegalStateException("Root input closed")
            if (line == end) break
            result.append(line).append('\n')
        }
        return result.toString()
    }

    fun foreground() = exchange("$focus >/dev/null && echo GAME").lines().any { it == "GAME" }
    // A zero-duration DOWN/UP can fall entirely between game input frames.
    // Keep the fire contact stationary across several frames, then release it.
    fun tap(x: Int, y: Int, valid: () -> Boolean) =
        send("input touchscreen swipe $x $y $x $y 80", valid)
    fun swipe(x: Int, y: Int, endX: Int, endY: Int, valid: () -> Boolean) =
        send("input touchscreen swipe $x $y $endX $endY 60", valid)
    private fun send(command: String, valid: () -> Boolean): Boolean =
        exchange("if $focus >/dev/null; then $command && echo SENT; fi", valid)
            .lines().any { it == "SENT" }

    override fun close() {
        process?.destroyForcibly()
        process = null
        reader = null
    }
}
