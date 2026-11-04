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
import android.view.Choreographer
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.WindowManager
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import kotlin.concurrent.thread
import kotlin.math.sqrt

class OverlayService : Service() {

    private lateinit var wm: WindowManager
    private val handler = Handler(Looper.getMainLooper())
    private var bubbleView: View? = null
    private var menuView: View? = null
    @Volatile private var isMenuOpen = false
    private var selectedTab = 0
    private var bubbleHidden = false
    private var overlaysHidden = false
    private val lifecycle = OverlayLifecycle()
    private var enemyLinesView: EnemyLinesView? = null
    val overlayDrawnFrames: Long get() = enemyLinesView?.drawnFrames ?: 0L
    val overlayVisible: Boolean get() = enemyLinesView?.visibility == View.VISIBLE
    @Volatile var lastPublishedSequence: Long = -1L
        private set
    private var bubbleX = 0; private var bubbleY = 0

    private val logLines = mutableListOf<String>()
    private val maxLogLines = 100
    private var logTextView: TextView? = null
    private var overlayFrameCallback: Choreographer.FrameCallback? = null
    private val projectionWork = ConflatedWork()
    private val publicationGate = FramePublicationGate()
    @Volatile private var lastProjectionLogAt = 0L

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        wm = getSystemService(Context.WINDOW_SERVICE) as WindowManager
        sInstance = this
        bubbleX = dpToPx(8); bubbleY = dpToPx(100)
        ensureEnemyLinesWindow()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_SHOW -> { lifecycle.show(); overlaysHidden = false; if (bubbleView == null && !isMenuOpen) showBubble() }
            ACTION_HIDE -> { lifecycle.hide(); stopOverlayRefresh(); overlaysHidden = true; enemyLinesView?.clear(); removeBubble(); if (isMenuOpen) closeMenu(false) }
            else -> { lifecycle.show(); overlaysHidden = false; if (bubbleView == null && !isMenuOpen) showBubble() }
        }
        return START_NOT_STICKY
    }

    private fun ensureEnemyLinesWindow() {
        if (enemyLinesView != null || !AndroidSettings.canDrawOverlays(this)) return
        val view = EnemyLinesView(this)
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

    /** May be called by the feed thread. Intermediate snapshots are not queued. */
    fun requestEnemyLinesRefresh() {
        if (!isMenuOpen && lifecycle.capture() != null && projectionWork.request()) dispatchProjection()
    }

    private fun dispatchProjection() {
        handler.post {
            Choreographer.getInstance().postFrameCallback { runProjection() }
        }
    }

    private fun runProjection() {
        try { refreshEnemyLines(::finishProjection) }
        catch (e: Exception) {
            android.util.Log.w("DeltaRadar", "Overlay projection: ${e.message}")
            finishProjection()
        }
    }

    private fun finishProjection() {
        if (projectionWork.finish()) dispatchProjection()
    }

    private fun publishProjection(finished: () -> Unit, publish: () -> Unit) {
        val apply = Runnable { try { publish() } finally { finished() } }
        if (Looper.myLooper() == Looper.getMainLooper()) apply.run() else handler.post(apply)
    }

    private fun refreshEnemyLines(finished: () -> Unit) {
        val generation = lifecycle.capture() ?: run { finished(); return }
        val service = EntityFeedService.instance
        if (service == null) {
            publishProjection(finished) { if (lifecycle.accepts(generation)) enemyLinesView?.clear() }
            return
        }
        val snapshot = service.getProjectionSnapshot()
        val selectedCamera = snapshot.camera
        val projectionTime = android.os.SystemClock.elapsedRealtime()
        if (snapshot.cameraRequired && selectedCamera?.usable(snapshot.json.optInt("gamePid"),
                snapshot.json.optString("world"), projectionTime) != true) {
            publishProjection(finished) { if (lifecycle.accepts(generation)) enemyLinesView?.clear() }
            return
        }
        if (snapshot.status.hasCamera && publicationGate.alreadyPublished(service, generation,
                snapshot.status.sequence, snapshot.cameraSequence)) { finished(); return }
        val selfId = snapshot.json.optString("self", "")
        if (!snapshot.status.hasCamera || selfId.isEmpty()) {
            publishProjection(finished) { if (lifecycle.accepts(generation)) enemyLinesView?.clear() }
            return
        }
        val entities = snapshot.json.getJSONArray("entities")

        var mx = 0.0; var my = 0.0; var mz = 0.0; var myYaw = 0.0; var myPitch = 0.0
        var cameraFov = 80.0
        var foundSelf = false
        for (i in 0 until entities.length()) {
            val e = entities.getJSONObject(i)
            if (e.optString("id") == selfId) {
                foundSelf = true
                mx = e.optDouble("x"); my = e.optDouble("y"); mz = e.optDouble("z")
                myYaw = e.optDouble("yaw"); myPitch = e.optDouble("pitch")
                cameraFov = e.optDouble("fov", 80.0).coerceIn(5.0, 150.0)
            }
        }
        if (!foundSelf) {
            publishProjection(finished) { if (lifecycle.accepts(generation)) enemyLinesView?.clear() }
            return
        }

        var cameraRoll = 0.0
        selectedCamera?.let { camera ->
            mx=camera.x; my=camera.y; mz=camera.z; myYaw=camera.yaw; myPitch=camera.pitch
            cameraRoll=camera.roll; cameraFov=camera.fov
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
        val lootValueFilter = Settings.lootValueFilter(this)
        val lootMinimumValue = Settings.lootMinimumValue(this)
        val renderDist = com.deltaradar.app.Settings.renderDistance(this).toDouble()

        val enemyFrames = mutableListOf<EnemyLinesView.Enemy>()
        val rejected = mutableListOf<String>()
        // Snapshot validation already guarantees unique IDs; do not rebuild a map.
        for (i in 0 until entities.length()) {
            val ent = entities.getJSONObject(i)
            val id = ent.getString("id")
            if (id == selfId) continue
            val kind = ent.optString("kind", "player")
            var name = ent.optString("name", id)
            if (kind == "loot") {
                val value = ent.optDouble("lootUnitValue", Double.NaN)
                val known = ent.optBoolean("lootValueKnown", false)
                val source = ent.optString("lootValueSource", "")
                if (!LootValuePolicy.passes(lootValueFilter, lootMinimumValue, known, source, value)) {
                    rejected.add("$id:loot-value"); continue
                }
                if (LootValuePolicy.knownValue(known, source, value))
                    name += " · ${LootValuePolicy.label(value)} each"
            }
            val combat = kind == "player" || kind == "ai"
            if (combat && ent.has("sampledElapsedMs") && projectionTime - ent.getLong("sampledElapsedMs") !in 0..150) continue
            if (combat && ent.has("positionPlaintext") && !ent.optBoolean("positionPlaintext")) continue
            val motion = WorldMotion.offset(WorldMotion.Velocity(ent.optDouble("velocityX",0.0),
                ent.optDouble("velocityY",0.0),ent.optDouble("velocityZ",0.0)),
                ent.optLong("sampledElapsedMs"),selectedCamera?.sampledAt ?: projectionTime)
            val ex = ent.optDouble("x") + motion.x
            val ey = ent.optDouble("y") + motion.y
            val baseZ = ent.optDouble("z") + motion.z
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
            val features = when {
                !isCombatEntity -> lootFeatures
                team == 1 -> teammateFeatures
                kind == "player" -> playerFeatures
                else -> botFeatures
            }
            if (features and Settings.FEATURE_ENABLED == 0) {
                rejected.add("$id:${if (isCombatEntity) "type-off" else "loot-off"}"); continue
            }

            val halfHeight = when (kind) {
                "vehicle" -> 1.0
                "loot" -> 0.15
                "objective" -> 0.5
                else -> ent.optDouble("halfHeight", 1.05).coerceIn(0.5, 2.5)
            }
            val point = ProjectionMath.project(
                ex - mx, ey - my, baseZ - mz, myYaw, myPitch, hfovDeg, screenAspect,
                clipToEdge = !isCombatEntity, rollDegrees = cameraRoll
            )
            // Only draw entities in the camera-facing hemisphere. Targets
            // behind the player must not produce bottom-edge tracers or labels.
            if (!point.inFront) { rejected.add("$id:behind"); continue }
            // RootComponent is the capsule centre; project its actual endpoints.
            val head = ProjectionMath.project(
                ex - mx, ey - my, baseZ - mz + halfHeight,
                myYaw, myPitch, hfovDeg, screenAspect, clipToEdge = false, rollDegrees = cameraRoll
            )
            val feet = ProjectionMath.project(
                ex - mx, ey - my, baseZ - mz - halfHeight,
                myYaw, myPitch, hfovDeg, screenAspect, clipToEdge = false, rollDegrees = cameraRoll
            )

            var bonePose: SkeletonProjection.Pose? = null
            if (combat && ent.optBoolean("bonesVerified", false)) {
                val bones = ent.getJSONArray("bones")
                val parents = ent.getJSONArray("boneParents")
                val joints = (0 until bones.length()).map { b ->
                    val joint = bones.getJSONArray(b)
                    SkeletonProjection.Joint(joint.getDouble(0)+motion.x, joint.getDouble(1)+motion.y,
                        joint.getDouble(2)+motion.z)
                }
                bonePose = SkeletonProjection.project(joints, IntArray(parents.length()) { parents.getInt(it) },
                    SkeletonProjection.Joint(mx,my,mz),myYaw,myPitch,hfovDeg,screenAspect,roll=cameraRoll)
            }
            val bounds = bonePose?.let { pose ->
                val valid = pose.x.indices.filter { pose.x[it].isFinite() && pose.y[it].isFinite() }
                if (valid.size < 2) null else floatArrayOf(
                    valid.minOf { pose.x[it] }-2f/metrics.widthPixels,
                    valid.minOf { pose.y[it] }-2f/metrics.heightPixels,
                    valid.maxOf { pose.x[it] }+2f/metrics.widthPixels,
                    valid.maxOf { pose.y[it] }+2f/metrics.heightPixels)
            }

            enemyFrames.add(EnemyLinesView.Enemy(
                id, name, distanceMeters, point.x, point.y, kind,
                point.onScreen,
                head.x, head.y, feet.x, feet.y,
                health = health.toFloat(), maxHealth = ent.optDouble("maxHealth", 100.0).toFloat(),
                healthKnown = healthKnown,
                boneScreenX = bonePose?.x, boneScreenY = bonePose?.y,
                boneParents = bonePose?.parents, bodyBounds = bounds,
                team = team
            ))
        }

        enemyFrames.sortBy { it.distanceMeters }
        val frame = EnemyLinesView.Frame(meScreen, enemyFrames)
        // Building and writing a full per-entity signature whenever any actor
        // crossed a one-metre boundary produced logcat traffic almost every
        // frame. On this device that competes with rendering and the scanner.
        // Keep a low-rate diagnostic count instead.
        val projectionNow = android.os.SystemClock.elapsedRealtime()
        if (projectionNow - lastProjectionLogAt >= 5_000L) {
            lastProjectionLogAt = projectionNow
            android.util.Log.i(
                "DeltaRadar",
                "Overlay frame: rendered=${enemyFrames.size} rejected=${rejected.size} feed=${service.entityCount}"
            )
        }
        publishProjection(finished) {
            if (!lifecycle.accepts(generation) || EntityFeedService.instance !== service) return@publishProjection
            if (!service.canRender(snapshot.status)) {
                if (!service.feedStatus.hasCamera) enemyLinesView?.clear()
                return@publishProjection
            }
            ensureEnemyLinesWindow()
            val view = enemyLinesView ?: return@publishProjection
            if (isMenuOpen) { view.clear(); return@publishProjection }
            if (!publicationGate.accept(service, generation, snapshot.status.sequence, snapshot.cameraSequence)) return@publishProjection
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
            val deadline = minOf(snapshot.status.receivedAtMs + EntitySnapshotStore.FRESHNESS_MS,
                selectedCamera?.let { it.sampledAt + 101 } ?: Long.MAX_VALUE)
            view.submit(frame, snapshot.status.receivedAtMs, deadline)
            lastPublishedSequence = snapshot.status.sequence
            CaptureDiagnostics.record(this, "latest-overlay-frame.json") {
                org.json.JSONObject().put("sequence", snapshot.status.sequence)
                    .put("dataReceivedElapsedMs", snapshot.status.receivedAtMs)
                    .put("aspect", screenAspect).put("cameraX", mx).put("cameraY", my).put("cameraZ", mz)
                    .put("yaw", myYaw).put("pitch", myPitch).put("fov", hfovDeg)
                    .put("projected", org.json.JSONArray().also { list ->
                        enemyFrames.forEach { e -> list.put(org.json.JSONObject()
                            .put("id", e.id).put("kind", e.kind).put("team", e.team)
                            .put("x", e.screenX.toDouble()).put("y", e.screenY.toDouble())
                            .put("onScreen", e.onScreen).put("distance", e.distanceMeters)) }
                    }).put("rejected", org.json.JSONArray(rejected))
            }
        }
    }

    private fun startOverlayRefresh() {
        stopOverlayRefresh()
        publicationGate.reset()
        val choreographer = Choreographer.getInstance()
        val callback = object : Choreographer.FrameCallback {
            override fun doFrame(frameTimeNanos: Long) {
                if (overlayFrameCallback !== this) return
                if (!isMenuOpen && lifecycle.capture() != null && projectionWork.request()) runProjection()
                if (overlayFrameCallback === this) choreographer.postFrameCallback(this)
            }
        }
        overlayFrameCallback = callback
        choreographer.postFrameCallback(callback)
    }

    private fun stopOverlayRefresh() {
        overlayFrameCallback?.let { Choreographer.getInstance().removeFrameCallback(it) }
        overlayFrameCallback = null
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
        stopOverlayRefresh()
        enemyLinesView?.clear()
        removeBubble()

        val view = LayoutInflater.from(this).inflate(R.layout.overlay_menu, null)
        val params = WindowManager.LayoutParams(
            WindowManager.LayoutParams.MATCH_PARENT, WindowManager.LayoutParams.MATCH_PARENT,
            WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL, PixelFormat.TRANSLUCENT
        )

        logTextView = view.findViewById(R.id.logText)
        synchronized(logLines) { logTextView?.text = logLines.joinToString("\n") }
        view.findViewById<ScrollView>(R.id.logScroll).post {
            view.findViewById<ScrollView>(R.id.logScroll).fullScroll(View.FOCUS_DOWN)
        }

        setupTabs(view)
        setupGeneralTab(view)
        setupVisualTab(view)

        view.findViewById<View>(R.id.btnClose).setOnClickListener { closeMenu() }
        view.findViewById<View>(R.id.menuBackground).setOnClickListener { closeMenu() }

        wm.addView(view, params)
        menuView = view
    }

    private fun setupTabs(view: View) {
        val tabs = listOf(R.id.tabGeneral, R.id.tabVisual, R.id.tabLogs)
        val panels = listOf(R.id.generalScroll, R.id.visualScroll, R.id.logScroll)
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
        select(selectedTab)
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
        if (type == Settings.EntityType.LOOT) setupLootValueControls(container)
        refresh()
    }

    private fun setupLootValueControls(container: LinearLayout) {
        val toggle = TextView(this).apply {
            textSize = 11f
            setTextColor(0xFFc8d6e0.toInt())
            setPadding(0, dpToPx(8), 0, dpToPx(8))
            isClickable = true
            isFocusable = true
        }
        fun refreshToggle() {
            toggle.text = "Valuable loot only: " + if (Settings.lootValueFilter(this)) "ON" else "OFF"
        }
        toggle.setOnClickListener {
            Settings.setLootValueFilter(this, !Settings.lootValueFilter(this))
            refreshToggle()
        }
        refreshToggle()
        container.addView(toggle)
        val thresholdLabel = TextView(this).apply {
            textSize = 11f
            setTextColor(0xFFc8d6e0.toInt())
        }
        fun refreshThreshold() {
            thresholdLabel.text = "Item value above ${LootValuePolicy.label(Settings.lootMinimumValue(this).toDouble())}"
        }
        refreshThreshold()
        container.addView(thresholdLabel)
        container.addView(SeekBar(this).apply {
            max = 500
            progress = Settings.lootMinimumValue(this@OverlayService) / 1000
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, value: Int, fromUser: Boolean) {
                    if (fromUser) {
                        Settings.setLootMinimumValue(this@OverlayService, value * 1000)
                        refreshThreshold()
                    }
                }
                override fun onStartTrackingTouch(bar: SeekBar?) {}
                override fun onStopTrackingTouch(bar: SeekBar?) {}
            })
        })
        container.addView(TextView(this).apply {
            text = "Filters ordinary collectibles by individual value. Keys, gear and unknown contents/prices are hidden when ON."
            textSize = 10f
            setTextColor(0xFF9AAAB8.toInt())
        })
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

    private fun updateToggle(tv: TextView, on: Boolean) {
        tv.text = if (on) "ON" else "OFF"
        tv.setTextColor(if (on) 0xFF4fd08a.toInt() else 0xFFe5534b.toInt())
    }

    private fun closeMenu(showBubbleAfter: Boolean = true) {
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
            menuView?.findViewById<ScrollView>(R.id.logScroll)?.fullScroll(View.FOCUS_DOWN)
        }
    }

    override fun onDestroy() {
        lifecycle.destroy()
        stopOverlayRefresh()
        projectionWork.close()
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
