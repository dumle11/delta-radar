package com.deltaradar.app

import android.app.Service
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.PixelFormat
import android.os.Build
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.provider.Settings as AndroidSettings
import android.view.Gravity
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.widget.Switch
import android.widget.Toast
import kotlin.concurrent.thread
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.sqrt

class OverlayService : Service() {

    private lateinit var wm: WindowManager
    private val handler = Handler(Looper.getMainLooper())
    private var bubbleView: View? = null
    private var menuView: View? = null
    @Volatile private var isMenuOpen = false
    private var selectedTab = 0
    private var bubbleHidden = false
    @Volatile private var overlaysHidden = false
    private var enemyLinesView: EnemyLinesView? = null
    private var aimFovView: AimFovView? = null
    @Volatile private var calibrationView: View? = null
    private lateinit var aimController: AimController
    private var bubbleX = 0; private var bubbleY = 0

    private val logLines = mutableListOf<String>()
    private val maxLogLines = 100
    private var logTextView: TextView? = null
    private var diagnosticRefreshRunnable: Runnable? = null
    private var overlayRefreshRunnable: Runnable? = null
    private val projectionExecutor = Executors.newSingleThreadExecutor { r ->
        Thread(r, "delta-projection").apply { isDaemon = true }
    }
    private val projectionInFlight = AtomicBoolean(false)
    @Volatile private var lastProjectionLogAt = 0L

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        wm = getSystemService(Context.WINDOW_SERVICE) as WindowManager
        sInstance = this
        aimController = AimController(this)
        bubbleX = dpToPx(8); bubbleY = dpToPx(100)
        ensureEnemyLinesWindow()
        ensureAimWindow()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_SHOW -> { overlaysHidden = false; if (bubbleView == null && !isMenuOpen) showBubble() }
            ACTION_HIDE -> { overlaysHidden = true; aimController.paused = true; aimController.frame = null; aimFovView?.visibility = View.GONE; enemyLinesView?.clear(); removeBubble(); if (isMenuOpen) closeMenu(false) }
            else -> { overlaysHidden = false; if (bubbleView == null && !isMenuOpen) showBubble() }
        }
        return START_NOT_STICKY
    }

    private fun ensureEnemyLinesWindow() {
        if (enemyLinesView != null || !AndroidSettings.canDrawOverlays(this)) return
        val view = EnemyLinesView(this) { menuView?.let { updateOverlayStatus(it) } }
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START; x = 0; y = 0
            alpha = 1f
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
                layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        try { wm.addView(view, params); enemyLinesView = view }
        catch (e: Exception) { android.util.Log.w("DeltaRadar", "Overlay window: ${e.message}") }
    }

    @Volatile private var refreshCount = 0L

    fun refreshEnemyLines() {
        refreshCount++
        val service = EntityFeedService.instance
        if (service == null) { aimController.frame = null; if (refreshCount % 300 == 1L) android.util.Log.w("DeltaRadar", "refresh: no EntityFeedService instance"); return }
        if (service.lastUpdateTs == 0L || System.currentTimeMillis() - service.lastUpdateTs > 750L) {
            aimController.frame = null
            handler.post { enemyLinesView?.clear() }
            return
        }
        val selfId = service.selfId
        if (selfId.isNullOrEmpty()) {
            if (refreshCount % 300 == 1L) android.util.Log.w("DeltaRadar", "refresh: no selfId, entities=${service.entityCount}")
            aimController.frame = null
            handler.post { enemyLinesView?.clear() }
            return
        }
        val snapshot = service.getSnapshot()
        val entities = snapshot.optJSONArray("entities") ?: return
        val byId = HashMap<String, org.json.JSONObject>(entities.length())

        var mx = 0.0; var my = 0.0; var mz = 0.0; var myYaw = 0.0; var myPitch = 0.0
        var cameraFov = 80.0
        var foundSelf = false
        var selfDead = true
        val sourceTimeMs = snapshot.optLong("ts", 0)
        val sourceFrame = snapshot.optLong("frameSequence", 0L)
        val cameraSampledAt = snapshot.optLong("cameraSampledAt", 0L)
        val cameraTime = snapshot.optDouble("cameraTimestamp", -1.0)
        val cameraPlaintext = snapshot.optBoolean("cameraPositionPlaintext", false)
        for (i in 0 until entities.length()) {
            val e = entities.getJSONObject(i)
            byId[e.optString("id")] = e
            if (e.optString("id") == selfId) {
                foundSelf = true
                selfDead = e.optBoolean("dead", false) ||
                    (e.optBoolean("healthKnown", false) && e.optDouble("health", 0.0) <= .01)
                mx = e.optDouble("x"); my = e.optDouble("y"); mz = e.optDouble("z")
                myYaw = e.optDouble("yaw"); myPitch = e.optDouble("pitch")
                cameraFov = e.optDouble("fov", 80.0).coerceIn(5.0, 150.0)
            }
        }
        if (!foundSelf) {
            aimController.frame = null
            handler.post { enemyLinesView?.clear() }
            return
        }

        val hfovDeg = cameraFov
        val metrics = android.util.DisplayMetrics()
        @Suppress("DEPRECATION")
        wm.defaultDisplay.getRealMetrics(metrics)
        val screenAspect = metrics.widthPixels.toDouble() / metrics.heightPixels.coerceAtLeast(1)
        // Tracer lines originate from the top-centre of the viewport. Entity
        // boxes and labels continue to use their projected world positions.
        val meScreen = Pair(0.5f, 0.02f)

        val playerFeatures = Settings.entityFeatures(this, Settings.EntityType.PLAYER)
        val botFeatures = Settings.entityFeatures(this, Settings.EntityType.BOT)
        val teammateFeatures = Settings.entityFeatures(this, Settings.EntityType.TEAMMATE)
        val lootFeatures = Settings.entityFeatures(this, Settings.EntityType.LOOT)
        val renderDist = com.deltaradar.app.Settings.renderDistance(this).toDouble()

        val enemyFrames = mutableListOf<EnemyLinesView.Enemy>()
        val aimTargets = mutableListOf<AimMath.Target>()
        val collectAimTargets = !selfDead && cameraPlaintext &&
            cameraTime > 0 && (Settings.aimEnabled(this) || Settings.autoShoot(this))
        val rejected = mutableListOf<String>()
        var hiddenPlayers = 0
        val projectionTime = System.currentTimeMillis()
        for ((id, ent) in byId) {
            if (id == selfId) continue
            val kind = ent.optString("kind", "player")
            if (PlayerPositionPolicy.hidePlayer(
                    kind, ent.optBoolean("positionPlaintext", false),
                    ent.optLong("seenFrame", -1L), sourceFrame,
                    cameraPlaintext, cameraTime, cameraSampledAt, projectionTime
                )) {
                hiddenPlayers++
                continue
            }
            val name = ent.optString("name", id)
            val ex = ent.optDouble("x")
            val ey = ent.optDouble("y")
            val baseZ = ent.optDouble("z")
            val team = ent.optInt("team", 0)
            val dx = ex - mx; val dy = ey - my; val dz = baseZ - mz
            val distanceMeters = sqrt(dx * dx + dy * dy + dz * dz)
            val isCombatEntity = kind == "player" || kind == "ai" || kind == "vehicle"
            // The actor root of a box being looted can be almost coincident
            // with the camera. The near guard exists only to reject duplicate
            // self/combat pawns; applying it to loot hid point-blank boxes.
            if ((isCombatEntity && distanceMeters < 0.5) || distanceMeters > renderDist) {
                rejected.add("$id:$kind:t$team:range:${distanceMeters.toInt()}m")
                continue
            }
            val health = ent.optDouble("health", 100.0)
            val healthKnown = ent.optBoolean("healthKnown", false)
            if (ent.optBoolean("dead", false) || (healthKnown && health <= 0.01)) {
                rejected.add("$id:dead"); continue
            }
            // Targeting is independent of each category's drawing switches.
            if (collectAimTargets && (kind == "player" || kind == "ai") && team == 2 &&
                healthKnown && health > .01 && sourceFrame > 0 &&
                ent.optLong("seenFrame", -1L) == sourceFrame &&
                ent.optBoolean("positionPlaintext", false)) {
                val hh = ent.optDouble("halfHeight", 1.05).coerceIn(.5, 2.5)
                val body = ProjectionMath.project(dx, dy, dz + hh * .2,
                    myYaw, myPitch, hfovDeg, screenAspect)
                val top = ProjectionMath.project(dx, dy, dz + hh,
                    myYaw, myPitch, hfovDeg, screenAspect)
                val bottom = ProjectionMath.project(dx, dy, dz - hh,
                    myYaw, myPitch, hfovDeg, screenAspect)
                val height = kotlin.math.abs(bottom.y - top.y)
                aimTargets.add(AimMath.Target(id, body.x, body.y,
                    height * .12f / screenAspect.toFloat(), height * .2f,
                    kind, team, onScreen = body.onScreen && top.inFront && bottom.inFront,
                    positionPlaintext = true, renderedOnScreen = ent.optBoolean("renderedOnScreen", false)))
            }
            val features = when {
                !isCombatEntity -> lootFeatures
                team == 1 -> teammateFeatures
                kind == "player" -> playerFeatures
                else -> botFeatures
            }
            if (isCombatEntity && features and Settings.FEATURE_ENABLED == 0) {
                rejected.add("$id:type-off"); continue
            }
            if (!isCombatEntity && features and Settings.FEATURE_ENABLED == 0) {
                rejected.add("$id:loot-off"); continue
            }

            val halfHeight = when (kind) {
                "vehicle" -> 1.0
                "loot" -> 0.15
                "objective" -> 0.5
                else -> ent.optDouble("halfHeight", 1.05).coerceIn(0.5, 2.5)
            }
            val point = ProjectionMath.project(
                ex - mx, ey - my, baseZ - mz, myYaw, myPitch, hfovDeg, screenAspect
            )
            // Only draw entities in the camera-facing hemisphere. Targets
            // behind the player must not produce bottom-edge tracers or labels.
            if (!point.inFront) { rejected.add("$id:behind"); continue }
            // The replicated actor origin sits above the rendered model's visual
            // centre in this build. Keep the measured capsule height, but lower
            // the whole box so its bottom follows the character's feet.
            // RootComponent is the centre of the character capsule.  Applying
            // an additional downward bias made boxes slide below operators on
            // hills and as the camera pitched.  Project the actual capsule
            // endpoints instead; perspective supplies the required scaling.
            val visualCenterDrop = 0.0
            val head = ProjectionMath.project(
                ex - mx, ey - my, baseZ - mz + halfHeight - visualCenterDrop,
                myYaw, myPitch, hfovDeg, screenAspect
            )
            val feet = ProjectionMath.project(
                ex - mx, ey - my, baseZ - mz - halfHeight - visualCenterDrop,
                myYaw, myPitch, hfovDeg, screenAspect
            )

            var boneScreenX: FloatArray? = null
            var boneScreenY: FloatArray? = null
            val bonesArr = if (PlayerPositionPolicy.useRawBones(kind)) ent.optJSONArray("bones") else null
            if (bonesArr != null && bonesArr.length() >= 16) {
                val bsx = FloatArray(bonesArr.length())
                val bsy = FloatArray(bonesArr.length())
                var allOnScreen = true
                for (b in 0 until bonesArr.length().coerceAtMost(16)) {
                    val bone = bonesArr.optJSONArray(b) ?: continue
                    val boneX = bone.optDouble(0)
                    val boneY = bone.optDouble(1)
                    val boneZ = bone.optDouble(2)
                    val bp = ProjectionMath.project(
                        boneX - mx, boneY - my, boneZ - mz,
                        myYaw, myPitch, hfovDeg, screenAspect
                    )
                    bsx[b] = bp.x
                    bsy[b] = bp.y
                    if (!bp.onScreen) allOnScreen = false
                }
                if (allOnScreen) {
                    boneScreenX = bsx
                    boneScreenY = bsy
                }
            }

            enemyFrames.add(EnemyLinesView.Enemy(
                id, name, distanceMeters, point.x, point.y, kind,
                point.onScreen,
                head.x, head.y, feet.x, feet.y,
                health = health.toFloat(), maxHealth = ent.optDouble("maxHealth", 100.0).toFloat(),
                healthKnown = healthKnown,
                boneScreenX = boneScreenX, boneScreenY = boneScreenY,
                team = team
            ))
        }

        aimController.frame = if (selfDead || !snapshot.optBoolean("localPlayerControlled", false) ||
            !cameraPlaintext || cameraTime <= 0) null
        else AimController.Frame(aimTargets, metrics.widthPixels, metrics.heightPixels, sourceTimeMs,
            cameraTime, myYaw, myPitch, hfovDeg, cameraSampledAt)
        enemyFrames.sortBy { it.distanceMeters }
        val warning = if (hiddenPlayers > 0)
            getString(R.string.unverified_players_hidden, hiddenPlayers) else null
        val frame = EnemyLinesView.Frame(meScreen, enemyFrames, warning)
        // Building and writing a full per-entity signature whenever any actor
        // crossed a one-metre boundary produced logcat traffic almost every
        // frame. On this device that competes with rendering and the scanner.
        // Keep a low-rate diagnostic count instead.
        val projectionNow = android.os.SystemClock.elapsedRealtime()
        if (projectionNow - lastProjectionLogAt >= 5_000L) {
            lastProjectionLogAt = projectionNow
            android.util.Log.i(
                "DeltaRadar",
                "Overlay frame: rendered=${enemyFrames.size} rejected=${rejected.size} " +
                    "unverifiedPlayers=$hiddenPlayers feed=${service.entityCount}"
            )
        }
        handler.post {
            ensureEnemyLinesWindow()
            val view = enemyLinesView ?: return@post
            if (isMenuOpen) { view.clear(); return@post }
            view.setStyle(playerFeatures, botFeatures, teammateFeatures, lootFeatures,
                com.deltaradar.app.Settings.playerColor(this),
                com.deltaradar.app.Settings.botColor(this),
                com.deltaradar.app.Settings.teammateColor(this),
                com.deltaradar.app.Settings.lootColor(this),
                com.deltaradar.app.Settings.playerDrawMode(this),
                com.deltaradar.app.Settings.botDrawMode(this),
                com.deltaradar.app.Settings.teammateDrawMode(this),
                com.deltaradar.app.Settings.lootDrawMode(this),
                com.deltaradar.app.Settings.lineThicknessPercent(this) / 100f)
            view.submit(frame, android.os.SystemClock.elapsedRealtime())
        }
    }

    private fun startOverlayRefresh() {
        stopOverlayRefresh()
        val r = object : Runnable {
            override fun run() {
                aimController.paused = overlaysHidden || isMenuOpen || calibrationView != null
                aimFovView?.apply {
                    radiusPercent = Settings.aimFov(this@OverlayService)
                    visibility = if (!aimController.paused && aimController.gameForeground &&
                        Settings.aimCircle(this@OverlayService)) View.VISIBLE else View.GONE
                }
                menuView?.findViewById<TextView>(R.id.aimStatus)?.apply {
                    val next = aimController.status
                    if (text.toString() != next) text = next
                }
                if (projectionInFlight.compareAndSet(false, true)) {
                    projectionExecutor.execute {
                        try { refreshEnemyLines() } finally { projectionInFlight.set(false) }
                    }
                }
                handler.postDelayed(this, 33)
            }
        }
        overlayRefreshRunnable = r
        handler.postDelayed(r, 50)
    }

    private fun stopOverlayRefresh() {
        overlayRefreshRunnable?.let { handler.removeCallbacks(it) }
        overlayRefreshRunnable = null
    }

    private fun showBubble() {
        if (bubbleView != null || overlaysHidden) return
        ensureEnemyLinesWindow()
        startOverlayRefresh()
        val view = LayoutInflater.from(this).inflate(R.layout.overlay_bubble, null)
        val params = WindowManager.LayoutParams(
            dpToPx(56), dpToPx(56),
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE,
            PixelFormat.TRANSLUCENT
        ).apply { gravity = Gravity.TOP or Gravity.START; x = bubbleX; y = bubbleY }

        var initX = 0; var initY = 0; var initTouchX = 0f; var initTouchY = 0f; var moved = false
        view.setOnTouchListener { _, event ->
            when (event.action) {
                MotionEvent.ACTION_DOWN -> { initX = params.x; initY = params.y; initTouchX = event.rawX; initTouchY = event.rawY; moved = false; true }
                MotionEvent.ACTION_MOVE -> {
                    val dx = event.rawX - initTouchX; val dy = event.rawY - initTouchY
                    if (dx * dx + dy * dy > 25) moved = true
                    params.x = initX + dx.toInt(); params.y = initY + dy.toInt()
                    wm.updateViewLayout(view, params); true
                }
                MotionEvent.ACTION_UP -> { bubbleX = params.x; bubbleY = params.y; if (!moved) openMenu(); true }
                else -> false
            }
        }
        if (bubbleHidden) view.alpha = 0f
        wm.addView(view, params)
        bubbleView = view
    }

    private fun removeBubble() {
        bubbleView?.let { try { wm.removeView(it) } catch (_: Exception) {} }
        bubbleView = null
    }

    private fun openMenu() {
        if (isMenuOpen) return
        isMenuOpen = true
        aimController.paused = true
        aimFovView?.visibility = View.GONE
        removeBubble()

        val view = LayoutInflater.from(this).inflate(R.layout.overlay_menu, null)
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL, PixelFormat.TRANSLUCENT
        )

        logTextView = view.findViewById(R.id.logText)
        synchronized(logLines) { logTextView?.text = logLines.joinToString("\n") }

        setupTabs(view)
        setupGeneralTab(view)
        setupVisualTab(view)
        setupAimTab(view)
        updateOverlayStatus(view)

        view.findViewById<View>(R.id.btnClose).setOnClickListener { closeMenu() }
        view.findViewById<View>(R.id.menuBackground).setOnClickListener { closeMenu() }

        wm.addView(view, params)
        menuView = view
        view.findViewById<TextView>(R.id.positionTraceToggle).setOnClickListener {
            val service = EntityFeedService.instance
            if (service == null) {
                Toast.makeText(this, "Entity feed is not running", Toast.LENGTH_SHORT).show()
            } else if (service.isPositionTraceRecording()) service.stopPositionTrace()
            else service.startPositionTrace()
        }
        val diagnosticRefresh = object : Runnable {
            override fun run() {
                if (menuView !== view) return
                if (selectedTab == 3) {
                    val service = EntityFeedService.instance
                    view.findViewById<TextView>(R.id.positionTraceToggle).text =
                        if (service?.isPositionTraceRecording() == true) "Stop position trace"
                        else "Start position trace"
                    view.findViewById<TextView>(R.id.positionTraceStatus).text =
                        (service?.positionTraceStatus() ?: "Trace unavailable: entity feed is not running") +
                            "\nPrivate files: files/position-trace-{current,previous}.jsonl"
                    view.findViewById<TextView>(R.id.diagnosticText).text = DiagnosticTable.format(
                        service?.getDiagnosticReport(), System.currentTimeMillis())
                }
                handler.postDelayed(this, 1_000)
            }
        }
        diagnosticRefreshRunnable = diagnosticRefresh
        handler.post(diagnosticRefresh)
    }

    private fun setupTabs(view: View) {
        val tabs = listOf(R.id.tabGeneral, R.id.tabVisual, R.id.tabAim, R.id.tabLogs)
        val panels = listOf(R.id.generalScroll, R.id.visualScroll, R.id.aimScroll, R.id.logScroll)
        fun select(index: Int) {
            selectedTab = index
            tabs.forEachIndexed { i, id ->
                view.findViewById<TextView>(id).apply {
                    isSelected = i == index
                    setTextColor(if (isSelected) 0xFF4fd08a.toInt() else 0xFF5c6b78.toInt())
                    setBackgroundColor(if (isSelected) 0xFF1E2940.toInt() else Color.TRANSPARENT)
                }
                view.findViewById<View>(panels[i]).visibility = if (i == index) View.VISIBLE else View.GONE
            }
        }
        tabs.forEachIndexed { i, id -> view.findViewById<View>(id).setOnClickListener { select(i) } }
        select(selectedTab.coerceIn(tabs.indices))
    }

    private fun ensureAimWindow() {
        if (aimFovView != null || !AndroidSettings.canDrawOverlays(this)) return
        val view = AimFovView(this)
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE or WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE or
                WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT
        ).apply {
            gravity = Gravity.TOP or Gravity.START
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
                layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        try { wm.addView(view, params); aimFovView = view }
        catch (e: Exception) { android.util.Log.w("DeltaRadar", "Aim circle: ${e.message}") }
    }

    private fun setupAimTab(view: View) {
        fun toggle(id: Int, value: Boolean, save: (Boolean) -> Unit) {
            view.findViewById<Switch>(id).apply {
                isChecked = value
                setOnCheckedChangeListener { _, checked -> save(checked) }
            }
        }
        toggle(R.id.aimEnabled, Settings.aimEnabled(this)) {
            Settings.setAimEnabled(this, it); aimController.resetFeedback()
        }
        toggle(R.id.aimCircle, Settings.aimCircle(this)) { Settings.setAimCircle(this, it) }
        toggle(R.id.aimAutoShoot, Settings.autoShoot(this)) { Settings.setAutoShoot(this, it) }
        fun slider(id: Int, label: Int, minimum: Int, value: Int, title: String, save: (Int) -> Unit) {
            val text = view.findViewById<TextView>(label)
            text.text = "$title: $value%"
            view.findViewById<SeekBar>(id).apply {
                progress = value - minimum
                setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                    override fun onProgressChanged(bar: SeekBar, progress: Int, fromUser: Boolean) {
                        val next = progress + minimum
                        text.text = "$title: $next%"
                        if (fromUser) save(next)
                    }
                    override fun onStartTrackingTouch(bar: SeekBar) {}
                    override fun onStopTrackingTouch(bar: SeekBar) {}
                })
            }
        }
        slider(R.id.aimFovSlider, R.id.aimFovLabel, 5, Settings.aimFov(this), "FOV radius") {
            Settings.setAimFov(this, it)
        }
        slider(R.id.aimStrengthSlider, R.id.aimStrengthLabel, 10, Settings.aimStrength(this), "Aim strength") {
            Settings.setAimStrength(this, it)
        }
        view.findViewById<TextView>(R.id.aimStatus).text = aimController.status
        val fire = Settings.aimFirePoint(this)
        val look = Settings.aimLookPoint(this)
        val lookLabel = "Camera drags start at ${(look.first * 100).toInt()}%, ${(look.second * 100).toInt()}%. No aim activation button is needed. "
        view.findViewById<TextView>(R.id.aimCalibration).text = lookLabel + if (fire == null)
            "Only auto shoot needs a saved fire button. Customize the drag area only if your HUD controls cover the default area."
        else "Fire button saved at ${(fire.first * 100).toInt()}%, ${(fire.second * 100).toInt()}%."
        view.findViewById<View>(R.id.aimSetLook).setOnClickListener { calibrateAimTouch(false) }
        view.findViewById<View>(R.id.aimDefaultLook).setOnClickListener {
            Settings.resetAimLookPoint(this)
            aimController.resetFeedback()
            setupAimTab(view)
        }
        view.findViewById<View>(R.id.aimSetFire).setOnClickListener { calibrateAimTouch(true) }
    }

    private fun calibrateAimTouch(fire: Boolean) {
        aimController.paused = true
        aimController.frame = null
        closeMenu(false)
        val capture = android.widget.FrameLayout(this).apply {
            setBackgroundColor(0x18000000)
            isFocusableInTouchMode = true
        }
        val instructions = TextView(this).apply {
            text = if (fire) "Tap the centre of your fire button · Back cancels"
                else "Tap an empty look area on the right side · Back cancels"
            setTextColor(Color.WHITE); setBackgroundColor(0xDD101820.toInt())
            gravity = Gravity.CENTER
            setPadding(16, 16, 16, 16)
        }
        capture.addView(instructions, android.widget.FrameLayout.LayoutParams(
            android.widget.FrameLayout.LayoutParams.MATCH_PARENT,
            android.widget.FrameLayout.LayoutParams.WRAP_CONTENT, Gravity.TOP))
        fun finish() {
            if (calibrationView !== capture) return
            try { wm.removeView(capture) } catch (_: Exception) {}
            calibrationView = null
            openMenu()
        }
        capture.setOnTouchListener { _, event ->
            if (event.actionMasked == MotionEvent.ACTION_UP) {
                if (event.y <= instructions.height) {
                    finish()
                } else if (!fire && !AimMath.isLookPoint(event.x / capture.width, event.y / capture.height)) {
                    Toast.makeText(this, "Choose an empty look area on the right side", Toast.LENGTH_SHORT).show()
                } else {
                    Settings.setAimTouchPoint(this, fire, event.x / capture.width, event.y / capture.height)
                    aimController.resetFeedback()
                    Toast.makeText(this, "Touch position saved", Toast.LENGTH_SHORT).show()
                    finish()
                }
            }
            true
        }
        capture.setOnKeyListener { _, keyCode, event ->
            if (keyCode == android.view.KeyEvent.KEYCODE_BACK) {
                if (event.action == android.view.KeyEvent.ACTION_UP) finish()
                true
            } else false
        }
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN or WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,
            PixelFormat.TRANSLUCENT
        ).apply {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P)
                layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
        }
        calibrationView = capture
        wm.addView(capture, params)
        capture.requestFocus()
        handler.postDelayed({ finish() }, 30_000)
    }

    private fun setupGeneralTab(view: View) {
        val service = EntityFeedService.instance
        val feedOn = service?.isFeedRunning == true
        val entityCount = service?.entityCount ?: 0

        view.findViewById<TextView>(R.id.statusState).apply {
            text = if (feedOn) "Overlay feed active" else "Waiting for feed"
            setTextColor(if (feedOn) 0xFF4fd08a.toInt() else 0xFF5c6b78.toInt())
        }
        view.findViewById<TextView>(R.id.statusDetails).text =
            "Entities: $entityCount"

        // Hide bubble toggle
        updateToggle(view.findViewById(R.id.toggleHideBubbleState), bubbleHidden)
        view.findViewById<LinearLayout>(R.id.toggleHideBubble).setOnClickListener {
            bubbleHidden = !bubbleHidden
            updateToggle(view.findViewById(R.id.toggleHideBubbleState), bubbleHidden)
        }
    }

    private fun setupVisualTab(view: View) {
        val lines = com.deltaradar.app.Settings.drawEnemyLines(this)
        val labels = com.deltaradar.app.Settings.showEnemyLabels(this)
        updateToggle(view.findViewById(R.id.toggleEnemyLinesState), lines)
        updateToggle(view.findViewById(R.id.toggleEnemyLabelsState), labels)

        view.findViewById<LinearLayout>(R.id.toggleEnemyLines).setOnClickListener {
            val enabled = !com.deltaradar.app.Settings.drawEnemyLines(this)
            com.deltaradar.app.Settings.setDrawEnemyLines(this, enabled)
            updateToggle(view.findViewById(R.id.toggleEnemyLinesState), enabled)
            updateOverlayStatus(view)
        }
        view.findViewById<LinearLayout>(R.id.toggleEnemyLabels).setOnClickListener {
            val enabled = !com.deltaradar.app.Settings.showEnemyLabels(this)
            com.deltaradar.app.Settings.setShowEnemyLabels(this, enabled)
            updateToggle(view.findViewById(R.id.toggleEnemyLabelsState), enabled)
            updateOverlayStatus(view)
        }

        val showPlayers = com.deltaradar.app.Settings.showPlayers(this)
        updateToggle(view.findViewById(R.id.toggleShowPlayersState), showPlayers)
        view.findViewById<LinearLayout>(R.id.toggleShowPlayers).setOnClickListener {
            val enabled = !com.deltaradar.app.Settings.showPlayers(this)
            com.deltaradar.app.Settings.setShowPlayers(this, enabled)
            updateToggle(view.findViewById(R.id.toggleShowPlayersState), enabled)
        }

        val showTeammates = com.deltaradar.app.Settings.showTeammates(this)
        updateToggle(view.findViewById(R.id.toggleShowTeammatesState), showTeammates)
        view.findViewById<LinearLayout>(R.id.toggleShowTeammates).setOnClickListener {
            val enabled = !com.deltaradar.app.Settings.showTeammates(this)
            com.deltaradar.app.Settings.setShowTeammates(this, enabled)
            updateToggle(view.findViewById(R.id.toggleShowTeammatesState), enabled)
        }

        val showLoot = com.deltaradar.app.Settings.showLoot(this)
        updateToggle(view.findViewById(R.id.toggleShowLootState), showLoot)
        view.findViewById<LinearLayout>(R.id.toggleShowLoot).setOnClickListener {
            val enabled = !com.deltaradar.app.Settings.showLoot(this)
            com.deltaradar.app.Settings.setShowLoot(this, enabled)
            updateToggle(view.findViewById(R.id.toggleShowLootState), enabled)
        }

        // Lines, names, distance and visibility are now configured per entity
        // type below. Keep the old rows hidden so upgraded installs can still
        // migrate their saved values without presenting conflicting controls.
        view.findViewById<View>(R.id.toggleEnemyLines).visibility = View.GONE
        view.findViewById<View>(R.id.enemyLinesDetails).visibility = View.GONE
        view.findViewById<View>(R.id.toggleEnemyLabels).visibility = View.GONE
        view.findViewById<View>(R.id.enemyLabelsDetails).visibility = View.GONE
        view.findViewById<View>(R.id.toggleShowPlayers).visibility = View.GONE
        view.findViewById<View>(R.id.toggleShowTeammates).visibility = View.GONE
        view.findViewById<View>(R.id.toggleShowLoot).visibility = View.GONE

        setupEntityFeaturePanel(view, R.id.playerFeatureContainer, Settings.EntityType.PLAYER)
        setupEntityFeaturePanel(view, R.id.teammateFeatureContainer, Settings.EntityType.TEAMMATE)
        setupEntityFeaturePanel(view, R.id.botFeatureContainer, Settings.EntityType.BOT)
        setupEntityFeaturePanel(view, R.id.lootFeatureContainer, Settings.EntityType.LOOT)

        // Render distance slider
        val distSeek = view.findViewById<SeekBar>(R.id.renderDistSeek)
        val distValue = view.findViewById<TextView>(R.id.renderDistValue)
        val currentDist = com.deltaradar.app.Settings.renderDistance(this)
        distSeek.max = 1000
        distSeek.progress = currentDist
        distValue.text = "${currentDist}m"
        distSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onStartTrackingTouch(s: SeekBar) {}
            override fun onProgressChanged(s: SeekBar, v: Int, fromUser: Boolean) {
                val clamped = v.coerceAtLeast(10)
                distValue.text = "${clamped}m"
            }
            override fun onStopTrackingTouch(s: SeekBar) {
                val clamped = s.progress.coerceAtLeast(10)
                s.progress = clamped
                com.deltaradar.app.Settings.setRenderDistance(this@OverlayService, clamped)
            }
        })

        val thicknessSeek = view.findViewById<SeekBar>(R.id.lineThicknessSeek)
        val thicknessValue = view.findViewById<TextView>(R.id.lineThicknessValue)
        val currentThickness = com.deltaradar.app.Settings.lineThicknessPercent(this)
        thicknessSeek.max = 275
        thicknessSeek.progress = currentThickness - 25
        thicknessValue.text = "$currentThickness%"
        thicknessSeek.setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
            override fun onStartTrackingTouch(s: SeekBar) {}
            override fun onProgressChanged(s: SeekBar, v: Int, fromUser: Boolean) {
                thicknessValue.text = "${v + 25}%"
            }
            override fun onStopTrackingTouch(s: SeekBar) {
                com.deltaradar.app.Settings.setLineThicknessPercent(this@OverlayService, s.progress + 25)
            }
        })

        // Player draw mode + color
        setupDrawModeButtons(view,
            listOf(R.id.playerModeOff, R.id.playerModeLines, R.id.playerModeBoxes, R.id.playerModeSkeleton),
            com.deltaradar.app.Settings.playerDrawMode(this), 0xFFE5534B.toInt()
        ) { mode -> com.deltaradar.app.Settings.setPlayerDrawMode(this, mode) }

        val playerPresets = listOf(
            R.id.playerColorRed to 0xFFE5534B.toInt(),
            R.id.playerColorCyan to 0xFF46D9FF.toInt(),
            R.id.playerColorYellow to 0xFFFFE066.toInt(),
            R.id.playerColorWhite to Color.WHITE,
            R.id.playerColorPurple to 0xFF9B59B6.toInt()
        )
        playerPresets.forEach { (id, c) ->
            view.findViewById<View>(id).setOnClickListener {
                com.deltaradar.app.Settings.setPlayerColor(this, c)
            }
        }

        setupDrawModeButtons(view,
            listOf(R.id.teammateModeOff, R.id.teammateModeLines, R.id.teammateModeBoxes, R.id.teammateModeSkeleton),
            com.deltaradar.app.Settings.teammateDrawMode(this), Settings.DEFAULT_TEAMMATE_COLOR
        ) { mode -> com.deltaradar.app.Settings.setTeammateDrawMode(this, mode) }

        val teammatePresets = listOf(
            R.id.teammateColorGreen to 0xFF4fd08a.toInt(),
            R.id.teammateColorCyan to 0xFF46D9FF.toInt(),
            R.id.teammateColorYellow to 0xFFFFE066.toInt(),
            R.id.teammateColorWhite to Color.WHITE,
            R.id.teammateColorPurple to 0xFF9B59B6.toInt()
        )
        teammatePresets.forEach { (id, c) ->
            view.findViewById<View>(id).setOnClickListener {
                com.deltaradar.app.Settings.setTeammateColor(this, c)
            }
        }

        // Bot draw mode + color
        setupDrawModeButtons(view,
            listOf(R.id.botModeOff, R.id.botModeLines, R.id.botModeBoxes, R.id.botModeSkeleton),
            com.deltaradar.app.Settings.botDrawMode(this), 0xFF46D9FF.toInt()
        ) { mode -> com.deltaradar.app.Settings.setBotDrawMode(this, mode) }

        val botPresets = listOf(
            R.id.botColorCyan to 0xFF46D9FF.toInt(),
            R.id.botColorGreen to 0xFF4fd08a.toInt(),
            R.id.botColorYellow to 0xFFFFE066.toInt(),
            R.id.botColorRed to 0xFFE5534B.toInt(),
            R.id.botColorOrange to 0xFFFF8C42.toInt()
        )
        botPresets.forEach { (id, c) ->
            view.findViewById<View>(id).setOnClickListener {
                com.deltaradar.app.Settings.setBotColor(this, c)
            }
        }

        setupDrawModeButtons(view,
            listOf(R.id.lootModeOff, R.id.lootModeLines, R.id.lootModeBoxes, R.id.lootModeMarker),
            com.deltaradar.app.Settings.lootDrawMode(this), Settings.LOOT_COLOR
        ) { mode -> com.deltaradar.app.Settings.setLootDrawMode(this, mode) }
        val lootPresets = listOf(
            R.id.lootColorGreen to 0xFF4fd08a.toInt(),
            R.id.lootColorCyan to 0xFF46D9FF.toInt(),
            R.id.lootColorYellow to 0xFFFFE066.toInt(),
            R.id.lootColorRed to 0xFFE5534B.toInt(),
            R.id.lootColorOrange to 0xFFFF8C42.toInt()
        )
        lootPresets.forEach { (id, c) ->
            view.findViewById<View>(id).setOnClickListener {
                com.deltaradar.app.Settings.setLootColor(this, c)
            }
        }
    }

    private fun setupEntityFeaturePanel(view: View, containerId: Int, type: Settings.EntityType) {
        val container = view.findViewById<LinearLayout>(containerId)
        container.removeAllViews()
        val states = mutableMapOf<Int, TextView>()

        fun refresh() {
            val mask = Settings.entityFeatures(this, type)
            states.forEach { (feature, state) -> updateToggle(state, mask and feature != 0) }
        }

        fun addRow(label: String, feature: Int) {
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = Gravity.CENTER_VERTICAL
                setPadding(0, 0, 0, 0)
                layoutParams = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT, dpToPx(34)
                )
                isClickable = true
                isFocusable = true
            }
            row.addView(TextView(this).apply {
                text = label
                textSize = 11f
                setTextColor(0xFFc8d6e0.toInt())
                layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f)
            })
            val state = TextView(this).apply {
                gravity = Gravity.CENTER
                textSize = 10f
                setTypeface(typeface, android.graphics.Typeface.BOLD)
                setBackgroundColor(0xFF1E2940.toInt())
                layoutParams = LinearLayout.LayoutParams(dpToPx(46), dpToPx(22))
            }
            states[feature] = state
            row.addView(state)
            row.setOnClickListener {
                Settings.setEntityFeature(this, type, feature,
                    !Settings.entityFeature(this, type, feature))
                refresh()
            }
            container.addView(row)
        }

        val quick = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            layoutParams = LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, dpToPx(30)
            )
        }
        fun quickButton(label: String, mask: Int) = TextView(this).apply {
            text = label
            gravity = Gravity.CENTER
            textSize = 10f
            setTextColor(0xFFc8d6e0.toInt())
            setBackgroundColor(0xFF1E2940.toInt())
            layoutParams = LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.MATCH_PARENT, 1f).apply {
                marginEnd = dpToPx(2)
            }
            setOnClickListener {
                Settings.setEntityFeatures(this@OverlayService, type, mask)
                if (mask == Settings.ALL_FEATURES) {
                    when (type) {
                        Settings.EntityType.PLAYER -> if (Settings.playerDrawMode(this@OverlayService) == Settings.DRAW_MODE_OFF)
                            Settings.setPlayerDrawMode(this@OverlayService, Settings.DRAW_MODE_BOXES)
                        Settings.EntityType.BOT -> if (Settings.botDrawMode(this@OverlayService) == Settings.DRAW_MODE_OFF)
                            Settings.setBotDrawMode(this@OverlayService, Settings.DRAW_MODE_BOXES)
                        Settings.EntityType.TEAMMATE -> if (Settings.teammateDrawMode(this@OverlayService) == Settings.DRAW_MODE_OFF)
                            Settings.setTeammateDrawMode(this@OverlayService, Settings.DRAW_MODE_BOXES)
                        Settings.EntityType.LOOT -> if (Settings.lootDrawMode(this@OverlayService) == Settings.DRAW_MODE_OFF)
                            Settings.setLootDrawMode(this@OverlayService, Settings.DRAW_MODE_BOXES)
                    }
                }
                refresh()
            }
        }
        quick.addView(quickButton("ALL ON", Settings.ALL_FEATURES))
        quick.addView(quickButton("ALL OFF", 0))
        container.addView(quick)
        addRow("Enabled", Settings.FEATURE_ENABLED)
        addRow("Tracer lines", Settings.FEATURE_LINES)
        addRow("Names", Settings.FEATURE_NAMES)
        addRow("Distance", Settings.FEATURE_DISTANCE)
        if (type != Settings.EntityType.LOOT) addRow("Health", Settings.FEATURE_HEALTH)
        addRow("Boxes / skeleton", Settings.FEATURE_BODY)
        refresh()
    }

    private fun setupDrawModeButtons(view: View, ids: List<Int>, currentMode: Int,
                                     accentColor: Int, onSelect: (Int) -> Unit) {
        val modes = listOf(Settings.DRAW_MODE_OFF, Settings.DRAW_MODE_LINES,
                           Settings.DRAW_MODE_BOXES, Settings.DRAW_MODE_SKELETON)
        val buttons = ids.map { view.findViewById<TextView>(it) }
        fun update(mode: Int) {
            buttons.forEachIndexed { i, btn ->
                val active = modes[i] == mode
                btn.setBackgroundColor(if (active) accentColor else 0xFF1E2940.toInt())
                btn.setTextColor(if (active) 0xFF0a0e12.toInt() else 0xFFc8d6e0.toInt())
            }
        }
        update(currentMode)
        buttons.forEachIndexed { i, btn ->
            btn.setOnClickListener { update(modes[i]); onSelect(modes[i]) }
        }
    }

    private fun updateOverlayStatus(view: View) {
        val lines = com.deltaradar.app.Settings.drawEnemyLines(this)
        val labels = com.deltaradar.app.Settings.showEnemyLabels(this)
        val service = EntityFeedService.instance
        val entities = service?.entityCount ?: 0
        view.findViewById<TextView>(R.id.enemyLinesDetails).text = when {
            !lines -> "Lines are disabled"
            entities == 0 -> "Waiting for entity data"
            else -> "Drawing lines to enemies"
        }
        view.findViewById<TextView>(R.id.enemyLabelsDetails).text = when {
            !labels -> "Labels are disabled"
            entities == 0 -> "Waiting for entity data"
            else -> "Showing name and distance"
        }
    }

    private fun updateToggle(tv: TextView, on: Boolean) {
        tv.text = if (on) "ON" else "OFF"
        tv.setTextColor(if (on) 0xFF4fd08a.toInt() else 0xFFe5534b.toInt())
    }

    private fun closeMenu(showBubbleAfter: Boolean = true) {
        diagnosticRefreshRunnable?.let { handler.removeCallbacks(it) }
        diagnosticRefreshRunnable = null
        menuView?.let { try { wm.removeView(it) } catch (_: Exception) {} }
        menuView = null; logTextView = null; isMenuOpen = false
        if (showBubbleAfter) showBubble()
    }

    fun addLog(line: String) {
        synchronized(logLines) {
            logLines.add(line)
            if (logLines.size > maxLogLines) logLines.removeAt(0)
        }
        handler.post {
            synchronized(logLines) { logTextView?.text = logLines.joinToString("\n") }
        }
    }

    override fun dump(fd: java.io.FileDescriptor, writer: java.io.PrintWriter, args: Array<out String>) {
        writer.println("Aim: ${aimController.status}")
        writer.println("Fault: ${aimController.faultMessage() ?: "none"}")
        writer.println("Gestures: ${aimController.gesturesSent}; confirmed: ${aimController.correctionsConfirmed}; fire presses: ${aimController.shotsSent}")
        writer.println("Look area: ${Settings.aimLookPoint(this)}")
    }

    override fun onDestroy() {
        diagnosticRefreshRunnable?.let { handler.removeCallbacks(it) }
        diagnosticRefreshRunnable = null
        aimController.close()
        calibrationView?.let { try { wm.removeView(it) } catch (_: Exception) {} }
        calibrationView = null
        aimFovView?.let { try { wm.removeView(it) } catch (_: Exception) {} }
        aimFovView = null
        stopOverlayRefresh()
        projectionExecutor.shutdownNow()
        enemyLinesView?.let { try { wm.removeView(it) } catch (_: Exception) {} }
        enemyLinesView = null
        removeBubble()
        menuView?.let { try { wm.removeView(it) } catch (_: Exception) {} }
        menuView = null
        sInstance = null
        super.onDestroy()
    }

    private fun dpToPx(dp: Int): Int = (dp * resources.displayMetrics.density).toInt()

    companion object {
        const val ACTION_SHOW = "com.deltaradar.OVERLAY_SHOW"
        const val ACTION_HIDE = "com.deltaradar.OVERLAY_HIDE"
        private var sInstance: OverlayService? = null
        val instance: OverlayService? get() = sInstance

        fun show(ctx: Context) {
            ctx.startService(Intent(ctx, OverlayService::class.java).apply { action = ACTION_SHOW })
        }
        fun hide(ctx: Context) {
            ctx.startService(Intent(ctx, OverlayService::class.java).apply { action = ACTION_HIDE })
        }
        fun stop(ctx: Context) { ctx.stopService(Intent(ctx, OverlayService::class.java)) }
    }
}
