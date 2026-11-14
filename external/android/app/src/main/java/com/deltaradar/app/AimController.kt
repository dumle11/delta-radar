package com.deltaradar.app

import android.content.Context
import android.os.SystemClock
import android.util.Log
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

/** Fresh-pose closed-loop aiming with independent physical and synthetic contacts. */
class AimController(private val context: Context) : AutoCloseable {
    data class Frame(val targets: List<AimMath.Target>, val width: Int, val height: Int,
                     val sourceTimeMs: Long, val cameraTime: Double,
                     val yaw: Double, val pitch: Double, val fov: Double, val poseTimeMs: Long,
                     val rotation: Int = 1, val session: String = "")
    @Volatile var frame: Frame? = null
    @Volatile var paused = true
    @Volatile var gameForeground = false; private set
    @Volatile var status = "Aim disabled"; private set
    @Volatile var gesturesSent = 0; private set
    @Volatile var shotsSent = 0; private set
    @Volatile var cameraSamplesSeen = 0; private set
    @Volatile var lastErrorPixels = 0f; private set
    @Volatile var physicalContacts = 0; private set
    @Volatile private var fault: String? = null
    @Volatile private var resetRequested = false
    @Volatile private var closed = false
    private val input = ContinuousTouchInput(context)
    private val servo = AimServo()
    private var lastInputFrame: Frame? = null
    private val focusProbe = AimFocusProbe()
    private val executor = Executors.newSingleThreadScheduledExecutor { r -> Thread(r, "delta-aim").apply { isDaemon = true } }
    private val focusExecutor = Executors.newSingleThreadScheduledExecutor { r -> Thread(r, "delta-aim-focus").apply { isDaemon = true } }
    private val traces = java.util.ArrayDeque<String>()
    fun traceDump(): String = synchronized(traces) { traces.joinToString("\n") }
    @Volatile private var targetId: String? = null
    fun lockedTargetId() = targetId
    private var lastSeen = 0L
    private var lastPhysicalContactAt = 0L
    private var session = ""
    private var manualLook = false
    private var contactReadyAt = 0L
    private var wasReady = false
    private var lastShot = 0L
    private var lastShotCameraTime = -1.0
    private var firingUntil = 0L
    private var alignedSince = 0L
    private var lastConfirmedCamera = -1.0
    fun faultMessage(): String? = fault
    fun resetFeedback() { resetRequested = true }

    init {
        // Foreground checks must never block a camera correction.
        focusExecutor.scheduleWithFixedDelay({
            if (!closed) try {
                gameForeground = focusProbe.gameForeground()
            } catch (_: Exception) { gameForeground = false }
        }, 0, 500, TimeUnit.MILLISECONDS)
        executor.scheduleWithFixedDelay({ tickSafely() }, 0, 16, TimeUnit.MILLISECONDS)
    }

    private fun tickSafely() {
        try { tick() } catch (e: Exception) {
            input.close(); servo.reset(); targetId = null
            fault = "Aim input stopped: ${e.message}"
            status = fault!!
            Log.w("DeltaRadar", status)
        }
    }

    private fun release(current: Frame?) {
        wasReady = false
        if (!input.started) return
        val geometry = current ?: lastInputFrame ?: return
        val response = input.step(geometry.width, geometry.height, geometry.rotation, Settings.aimLookPoint(context),
            0f, 0f, false, null, false)
        if (response.physicalContacts > 0) lastPhysicalContactAt = SystemClock.elapsedRealtime()
    }

    private fun tick() {
        if (closed) return
        if (resetRequested) {
            resetRequested = false; fault = null; targetId = null; servo.recalibrate(); alignedSince = 0
        }
        val aim = Settings.aimEnabled(context)
        val shoot = Settings.autoShoot(context)
        val current = frame
        if (current != null) lastInputFrame = current
        val now = SystemClock.elapsedRealtime()
        if ((!aim && !shoot) || paused || !gameForeground || current == null ||
            System.currentTimeMillis() - current.poseTimeMs !in 0..100) {
            if (paused && (aim || shoot) && current != null)
                input.step(current.width, current.height, current.rotation, Settings.aimLookPoint(context),
                    0f, 0f, false, null, false)
            release(current); servo.reset(); alignedSince = 0
            if ((!aim && !shoot) || paused || !gameForeground || now - lastSeen >= 750) targetId = null
            if (!aim && !shoot) { fault = null; input.close() }
            status = when {
                !aim && !shoot -> "Aim disabled"
                fault != null -> fault!!
                paused -> "Paused while menu is open"
                !gameForeground -> "Waiting for game"
                else -> "Waiting for a fresh camera and player"
            }
            return
        }
        if (current.session != session) {
            release(current); servo.reset(); targetId = null; session = current.session; fault = null
        }
        if (fault != null) { release(current); status = fault!!; return }
        val mask = Settings.aimTargetMask(context)
        if (mask == 0) { release(current); targetId = null; status = "No target types selected"; return }
        // A short loss of current data releases input but does not jump to a
        // different actor. Confirmed death/type exclusion releases the lock.
        val previous = current.targets.firstOrNull { it.id == targetId }
        if (previous != null && (!previous.alive || !AimMath.allowed(previous, mask) ||
                !previous.renderedOnScreen || !previous.visibleFromCamera || !previous.onScreen)) targetId = null
        val target = if (targetId != null && previous == null && now - lastSeen < 750) null else
            AimMath.select(current.targets, current.width, current.height, Settings.aimFov(context), targetId, mask)
        if (target == null) {
            release(current); alignedSince = 0; servo.reset()
            if (now - lastSeen >= 750) targetId = null
            status = "No current eligible target in FOV"
            return
        }
        if (targetId != target.id) { release(current); servo.reset(); alignedSince = 0 }
        targetId = target.id; lastSeen = now
        lastErrorPixels = kotlin.math.hypot((target.x - .5f) * current.width, (target.y - .5f) * current.height)
        if (AimMath.crosshairOnBody(target)) {
            if (alignedSince == 0L) alignedSince = now
        } else alignedSince = 0
        val fire = Settings.aimFirePoint(context)
        if (shoot && fire != null && alignedSince > 0 && now - alignedSince >= 80 && now - lastShot >= 200 && current.cameraTime > lastShotCameraTime) {
            lastShot = now; lastShotCameraTime = current.cameraTime; firingUntil = now + 80; shotsSent++
        }
        // Recheck freshness after helper startup or scheduling delays.
        if (frame?.session != current.session || paused || !gameForeground ||
            System.currentTimeMillis() - current.poseTimeMs !in 0..100) { release(current); return }
        val anchor = Settings.aimLookPoint(context)
        var response = input.step(current.width, current.height, current.rotation, anchor,
            0f, 0f, aim, fire, shoot && now < firingUntil)
        if (response.ready && !wasReady) contactReadyAt = now + 60
        wasReady = response.ready && !response.manualLook
        manualLook = response.manualLook
        physicalContacts = response.physicalContacts
        if (physicalContacts > 0) lastPhysicalContactAt = now
        if (!response.ready) servo.reset()
        else if (aim && now >= contactReadyAt) {
            val strength = if (manualLook) (Settings.aimStrength(context) * 0.4).toInt().coerceAtLeast(10)
                           else Settings.aimStrength(context)
            val movement = servo.step(target, current.width, current.height,
                current.cameraTime, current.yaw, current.pitch, current.fov, strength, now,
                physicalContacts == 0 && now - lastPhysicalContactAt >= 400 && now > firingUntil + 400)
            synchronized(traces) {
                traces.addLast("pose=${current.cameraTime} yaw=${current.yaw} pitch=${current.pitch} fov=${current.fov} target=${target.x},${target.y} move=$movement physical=$physicalContacts ${servo.diagnostics()}")
                while (traces.size > 24) traces.removeFirst()
            }
            if (servo.fault != null) { fault = servo.fault; release(current); status = fault!!; return }
            if (movement.first != 0f || movement.second != 0f) {
                response = input.step(current.width, current.height, current.rotation, anchor,
                    movement.first, movement.second, true, fire, shoot && now < firingUntil)
                if (!response.movementApplied) { servo.cancelUnapplied(); contactReadyAt = now + 60 }
                else gesturesSent++
                if (response.manualLook) servo.reset()
            }
        }
        if (current.cameraTime != lastConfirmedCamera) { lastConfirmedCamera = current.cameraTime; cameraSamplesSeen++ }
        status = when {
            !response.ready -> "Release the screen to start touch tracking"
            manualLook -> "Manual camera control · target retained"
            shoot && fire == null -> "Tracking ${target.name.ifBlank { "actor" }} · set fire button for auto shoot"
            else -> "Locked ${target.name.ifBlank { "actor" }} · response ${Settings.aimStrength(context)}%"
        }
    }

    override fun close() {
        closed = true; paused = true; frame = null
        executor.shutdownNow(); focusExecutor.shutdownNow(); focusProbe.close(); input.close()
    }
}

private class AimFocusProbe : AutoCloseable {
    @Volatile private var process: Process? = null
    private var reader: java.io.BufferedReader? = null
    fun gameForeground(): Boolean {
        var p = process
        if (p == null || !p.isAlive) {
            p = ProcessBuilder("su").redirectErrorStream(true).start()
            process = p; reader = p.inputStream.bufferedReader()
        }
        val marker = "DELTA_FOCUS_${System.nanoTime()}"
        p.outputStream.apply { write("dumpsys window displays\necho $marker\n".toByteArray()); flush() }
        var game = false
        while (true) {
            val line = reader?.readLine() ?: throw IllegalStateException("Focus probe closed")
            if (line == marker) break
            if (line.contains("mCurrentFocus=") && line.contains("com.proxima.dfm/")) game = true
        }
        return game
    }
    override fun close() { process?.destroy(); process = null }
}
