package com.deltaradar.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.os.SystemClock
import android.view.View
import kotlin.math.roundToInt

class EnemyLinesView(context: Context) : View(context) {
    companion object {
        const val FRESHNESS_MS = EntitySnapshotStore.FRESHNESS_MS
    }

    data class Enemy(
        val id: String,
        val name: String,
        val distanceMeters: Double,
        val screenX: Float,
        val screenY: Float,
        val kind: String = "player",
        val onScreen: Boolean = true,
        val headX: Float = screenX,
        val headY: Float = screenY,
        val feetX: Float = screenX,
        val feetY: Float = screenY,
        val health: Float = 100f,
        val maxHealth: Float = 100f,
        val healthKnown: Boolean = false,
        val boneScreenX: FloatArray? = null,
        val boneScreenY: FloatArray? = null,
        val boneParents: IntArray? = null,
        val bodyBounds: FloatArray? = null,
        val team: Int = 2,
        val containerGuide: Boolean = false,
        val lootValue: Double = Double.NaN
    )
    data class Frame(val me: Pair<Float, Float>?, val enemies: List<Enemy>,
                     val suppressNonPlayer: Boolean = false,
                     val nearbyContainerCount: Int = 0)

    @Volatile var drawnFrames = 0L
        private set
    private var frame: Frame? = null
    private var expiresAt = 0L
    private var sourceReceivedAt = 0L
    private var playerFeatures = 0
    private var botFeatures = 0
    private var teammateFeatures = 0
    private var lootFeatures = 0
    private var containerGuideEnabled = false
    private var containerFeatures = LootDisplayPolicy.CONTAINER_FEATURES_BASE
    private var lootOpacityPercent = 100
    private var lootMinimumValue = 20_000
    private var playerColor = Settings.DEFAULT_PLAYER_COLOR
    private var botColor = Settings.DEFAULT_BOT_COLOR
    private var teammateColor = Settings.DEFAULT_TEAMMATE_COLOR
    private var lootColor = Settings.LOOT_COLOR
    private var playerDrawMode = Settings.DRAW_MODE_BOXES
    private var botDrawMode = Settings.DRAW_MODE_BOXES
    private var teammateDrawMode = Settings.DRAW_MODE_BOXES
    private var lootDrawMode = Settings.DRAW_MODE_BOXES
    private var thicknessScale = 0.75f
    private val density = resources.displayMetrics.density

    private val linePaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeCap = Paint.Cap.ROUND
    }
    private val textPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = Color.WHITE
        textSize = 9 * resources.displayMetrics.scaledDensity
        typeface = android.graphics.Typeface.create(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD)
    }
    private val labelBgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xE6141A25.toInt() }
    private val labelOutline = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFF090D16.toInt(); style = Paint.Style.STROKE; strokeWidth = 2 * density
    }
    private val boxPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE; strokeWidth = 2 * density
    }
    private val boxShadowPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xAA000000.toInt(); style = Paint.Style.STROKE; strokeWidth = 3.5f * density
    }
    private val healthBgPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = 0xCC000000.toInt() }
    private val healthFillPaint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val rounded = RectF()
    private val lootEdgeMarker = Path()
    private val candidateLabel = RectF()
    private val labelBounds = Array(EntitySnapshotStore.MAX_ENTITIES) { RectF() }
    private val fontMetrics = Paint.FontMetrics()
    private val expire = Runnable { clear() }

    init {
        visibility = GONE
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
        isClickable = false; isFocusable = false
    }

    fun setStyle(pFeatures: Int, bFeatures: Int, tFeatures: Int, lFeatures: Int,
                 pColor: Int, bColor: Int, tColor: Int, lColor: Int,
                 pMode: Int, bMode: Int, tMode: Int, lMode: Int, thickness: Float,
                 containerGuideEnabled: Boolean = false,
                 containerFeatures: Int = LootDisplayPolicy.CONTAINER_FEATURES_BASE,
                 lootOpacityPercent: Int = 100,
                 lootMinimumValue: Int = 20_000) {
        if (playerFeatures == pFeatures && botFeatures == bFeatures && teammateFeatures == tFeatures &&
            lootFeatures == lFeatures && playerColor == pColor && botColor == bColor &&
            teammateColor == tColor && lootColor == lColor &&
            playerDrawMode == pMode && botDrawMode == bMode && teammateDrawMode == tMode &&
            lootDrawMode == lMode &&
            thicknessScale == thickness && this.containerGuideEnabled == containerGuideEnabled &&
            this.containerFeatures == containerFeatures &&
            this.lootOpacityPercent == lootOpacityPercent &&
            this.lootMinimumValue == lootMinimumValue) return
        this.containerGuideEnabled = containerGuideEnabled
        this.containerFeatures = containerFeatures
        this.lootOpacityPercent = lootOpacityPercent
        this.lootMinimumValue = lootMinimumValue
        playerFeatures = pFeatures; botFeatures = bFeatures; teammateFeatures = tFeatures; lootFeatures = lFeatures
        playerColor = pColor; botColor = bColor; teammateColor = tColor; lootColor = lColor
        playerDrawMode = pMode; botDrawMode = bMode; teammateDrawMode = tMode; lootDrawMode = lMode
        thicknessScale = thickness.coerceIn(0.25f, 3f)
        if (pFeatures == 0 && bFeatures == 0 && tFeatures == 0 && lFeatures == 0 && !containerGuideEnabled) clear()
        else if (frame != null) invalidate()
    }

    fun submit(value: Frame, receivedAtMs: Long, expiresAtMs: Long = receivedAtMs + FRESHNESS_MS) {
        if (value.me == null || value.enemies.isEmpty()) { clear(); return }
        frame = value; expiresAt = expiresAtMs; sourceReceivedAt = receivedAtMs
        removeCallbacks(expire)
        visibility = VISIBLE
        postInvalidateOnAnimation()
        postDelayed(expire, (expiresAt - SystemClock.elapsedRealtime()).coerceAtLeast(1))
    }

    fun clear() {
        removeCallbacks(expire); frame = null; visibility = GONE
    }

    override fun onDetachedFromWindow() { clear(); super.onDetachedFromWindow() }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val data = frame ?: return
        if (SystemClock.elapsedRealtime() >= expiresAt || width <= 0 || height <= 0) {
            post(expire); return
        }
        val me = data.me ?: return
        drawnFrames++
        CaptureDiagnostics.record(context, "latest-overlay-draw.json") {
            org.json.JSONObject().put("drawnFrames", drawnFrames).put("width", width).put("height", height)
                .put("sourceAgeMs", SystemClock.elapsedRealtime() - sourceReceivedAt)
                .put("entities", data.enemies.size).put("playerFeatures", playerFeatures)
                .put("botFeatures", botFeatures).put("lootFeatures", lootFeatures)
        }
        val padding = 2f * density
        var occupiedCount = 0
        val sorted = data.enemies.sortedBy { e ->
            when {
                e.kind == "player" && e.team != 1 -> 2
                e.kind == "player" || e.kind == "ai" -> 1
                else -> 0
            }
        }
        for (enemy in sorted) {
            // Tracers, labels and boxes must use the same camera sample.
            // Blending screen coordinates adds lag during every camera turn.
            val ex = enemy.screenX * width
            val ey = enemy.screenY * height
            val mx = me.first * width
            val my = me.second * height
            val isLoot = enemy.kind == "loot" || enemy.kind == "objective"
            val isPlayer = enemy.kind == "player"
            val isCombatant = isPlayer || enemy.kind == "ai"
            if (data.suppressNonPlayer && !isCombatant) continue
            val guide = enemy.containerGuide && containerGuideEnabled
            if ((guide || isLoot) && enemy.distanceMeters > LootDisplayPolicy.LOOT_MAX_DISTANCE) continue
            val baseColor = when {
                guide || isLoot -> LootDisplayPolicy.lootDistanceColor(enemy.distanceMeters)
                enemy.team == 1 -> teammateColor
                isPlayer -> playerColor
                else -> botColor
            }
            val highValue = !guide && isLoot && enemy.lootValue > lootMinimumValue.toDouble()
            val medValue = !guide && isLoot && !highValue && enemy.lootValue >= 10_000.0
            val notableValue = highValue || medValue
            val valueColor = when {
                highValue -> 0xFFE5534B.toInt()
                medValue -> 0xFFFFD700.toInt()
                else -> baseColor
            }
            val entityColor = if ((guide || isLoot) && !notableValue && lootOpacityPercent < 100)
                LootDisplayPolicy.applyOpacity(baseColor, lootOpacityPercent) else valueColor
            val entityMode = when {
                guide -> Settings.DRAW_MODE_BOXES
                isLoot -> lootDrawMode
                enemy.team == 1 -> teammateDrawMode
                isPlayer -> playerDrawMode
                else -> botDrawMode
            }
            val features = when {
                guide -> containerFeatures
                isLoot -> lootFeatures
                enemy.team == 1 -> teammateFeatures
                isPlayer -> playerFeatures
                else -> botFeatures
            }
            if (features and Settings.FEATURE_ENABLED == 0) continue
            val drawTracer = features and Settings.FEATURE_LINES != 0
            val drawName = features and Settings.FEATURE_NAMES != 0
            val drawDistance = features and Settings.FEATURE_DISTANCE != 0
            val drawHealth = features and Settings.FEATURE_HEALTH != 0
            val drawBody = features and Settings.FEATURE_BODY != 0 &&
                entityMode > Settings.DRAW_MODE_LINES
            if (!drawBody && !drawTracer && !drawName && !drawDistance && !drawHealth) continue
            var labelCenterX = ex
            var labelTopY = ey

            val applyLootFade = (guide || isLoot) && !notableValue && lootOpacityPercent < 100
            if (drawTracer) {
                val shadowAlpha = if (applyLootFade) (0xD5 * lootOpacityPercent / 100).coerceIn(1, 255) else 0xD5
                linePaint.color = (shadowAlpha shl 24) or 0x090D16
                linePaint.strokeWidth = 3f * density * thicknessScale
                canvas.drawLine(mx, my, ex, ey, linePaint)
                linePaint.color = entityColor
                linePaint.strokeWidth = density * thicknessScale
                canvas.drawLine(mx, my, ex, ey, linePaint)
            }

            val savedBoxShadowAlpha = boxShadowPaint.alpha
            if (applyLootFade) boxShadowPaint.alpha = (savedBoxShadowAlpha * lootOpacityPercent / 100).coerceIn(1, 255)
            var boxHeightForLabel = 48f * density
            if (enemy.kind == "loot" && !enemy.onScreen && drawBody) {
                // A nearby safe's actor origin can be below the camera's view.
                // Keep its direction visible without drawing a misplaced world
                // box: an inset diamond marks the clipped screen-edge position.
                val radius = 6f * density
                val inset = radius + padding
                val edgeX = ex.coerceIn(inset, (width - inset).coerceAtLeast(inset))
                val edgeY = ey.coerceIn(inset, (height - inset).coerceAtLeast(inset))
                lootEdgeMarker.reset()
                lootEdgeMarker.moveTo(edgeX, edgeY - radius)
                lootEdgeMarker.lineTo(edgeX + radius, edgeY)
                lootEdgeMarker.lineTo(edgeX, edgeY + radius)
                lootEdgeMarker.lineTo(edgeX - radius, edgeY)
                lootEdgeMarker.close()
                boxPaint.color = entityColor
                boxPaint.strokeWidth = density * thicknessScale
                boxShadowPaint.strokeWidth = 2.5f * density * thicknessScale
                canvas.drawPath(lootEdgeMarker, boxShadowPaint)
                canvas.drawPath(lootEdgeMarker, boxPaint)
                labelCenterX = edgeX
                labelTopY = edgeY - radius
            }
            if ((enemy.onScreen || enemy.bodyBounds != null) && (drawBody || drawHealth)) {
                val headX = enemy.headX * width
                val headY = enemy.headY * height
                val feetX = enemy.feetX * width
                val feetY = enemy.feetY * height
                val body = enemy.bodyBounds
                var top = body?.get(1)?.times(height) ?: minOf(headY, feetY)
                var bottom = body?.get(3)?.times(height) ?: maxOf(headY, feetY)
                // Container markers must remain visible at navigation range.
                if (guide && bottom - top < 8f * density) {
                    val center = (top + bottom) * 0.5f
                    top = center - 4f * density
                    bottom = center + 4f * density
                }
                val boxH = (bottom - top).coerceAtLeast(1f)
                boxHeightForLabel = boxH
                val centerX = body?.let { (it[0] + it[2]) * width * .5f } ?: ((headX + feetX) * .5f)
                labelCenterX = centerX
                labelTopY = top

                val strokeScale = if (guide) 0.5f else 1f
                boxPaint.color = entityColor
                boxPaint.strokeWidth = density * thicknessScale * strokeScale
                boxShadowPaint.strokeWidth = 2.5f * density * thicknessScale * strokeScale
                val healthBoxW = boxH * when (enemy.kind) {
                    "vehicle" -> 0.9f
                    "loot" -> 0.65f
                    else -> 0.45f
                }
                val healthLeft = body?.get(0)?.times(width) ?: (centerX - healthBoxW * 0.5f)
                when (if (drawBody) entityMode else Settings.DRAW_MODE_OFF) {
                    Settings.DRAW_MODE_SKELETON -> {
                        val bx = enemy.boneScreenX
                        val by = enemy.boneScreenY
                        val parents = enemy.boneParents
                        if (bx != null && by != null && parents != null && bx.size == by.size && bx.size == parents.size) {
                            for (child in 1 until parents.size) {
                                val parent = parents[child]
                                if (parent !in 0 until child || !bx[child].isFinite() || !by[child].isFinite() ||
                                    !bx[parent].isFinite() || !by[parent].isFinite()) continue
                                canvas.drawLine(bx[parent]*width,by[parent]*height,bx[child]*width,by[child]*height,boxShadowPaint)
                                canvas.drawLine(bx[parent]*width,by[parent]*height,bx[child]*width,by[child]*height,boxPaint)
                            }
                        }
                    }
                    Settings.DRAW_MODE_BOXES -> {
                        val boxW = (boxH * when (enemy.kind) {
                            "vehicle" -> 0.9f
                            "loot" -> 0.65f
                            else -> 0.45f
                        }).coerceAtLeast(if (guide) 8f * density else 0f)
                        val left = centerX - boxW * 0.5f
                        val right = centerX + boxW * 0.5f
                        val bounds = enemy.bodyBounds
                        val l = bounds?.get(0)?.times(width) ?: left
                        val t = bounds?.get(1)?.times(height) ?: top
                        val r = bounds?.get(2)?.times(width) ?: right
                        val b = bounds?.get(3)?.times(height) ?: bottom
                        canvas.drawRect(l, t, r, b, boxShadowPaint)
                        canvas.drawRect(l, t, r, b, boxPaint)
                    }
                }
                if (drawHealth && enemy.healthKnown && enemy.maxHealth > 0f) {
                    val hpFrac = (enemy.health / enemy.maxHealth).coerceIn(0f, 1f)
                    val barW = 1.5f * density
                    val barGap = 2f * density
                    val barLeft = healthLeft - barW - barGap
                    canvas.drawRect(barLeft, top, barLeft + barW, bottom, healthBgPaint)
                    val fillTop = top + (bottom - top) * (1f - hpFrac)
                    val r = ((1f - hpFrac) * 255).toInt().coerceIn(0, 255)
                    val g = (hpFrac * 255).toInt().coerceIn(0, 255)
                    healthFillPaint.color = Color.argb(220, r, g, 0)
                    canvas.drawRect(barLeft, fillTop, barLeft + barW, bottom, healthFillPaint)
                }
            }

            if (applyLootFade) boxShadowPaint.alpha = savedBoxShadowAlpha
            if (drawName || drawDistance) {
                val applyLootAlpha = (guide || isLoot) && !notableValue && lootOpacityPercent < 100
                val savedTextColor = textPaint.color
                val savedTextAlpha = textPaint.alpha
                val savedBgColor = labelBgPaint.color
                val savedBgAlpha = labelBgPaint.alpha
                val savedOutlineColor = labelOutline.color
                val savedOutlineAlpha = labelOutline.alpha
                if (highValue) {
                    textPaint.color = 0xFFFFFFFF.toInt()
                    labelBgPaint.color = 0xE6801A1A.toInt()
                    labelOutline.color = 0xFFE5534B.toInt()
                } else if (medValue) {
                    textPaint.color = 0xFF000000.toInt()
                    labelBgPaint.color = 0xE6FFD700.toInt()
                    labelOutline.color = 0xFFCCAA00.toInt()
                }
                if (applyLootAlpha) {
                    textPaint.alpha = (textPaint.alpha * lootOpacityPercent / 100).coerceIn(1, 255)
                    labelBgPaint.alpha = (labelBgPaint.alpha * lootOpacityPercent / 100).coerceIn(1, 255)
                    labelOutline.alpha = (labelOutline.alpha * lootOpacityPercent / 100).coerceIn(1, 255)
                }
                var labelScale = if (enemy.onScreen)
                    (boxHeightForLabel / (72f * density)).coerceIn(0.45f, 1f)
                else 0.6f
                if (guide && enemy.distanceMeters > 60.0)
                    labelScale *= (1f - ((enemy.distanceMeters - 60.0) / 140.0).coerceIn(0.0, 0.4).toFloat())
                textPaint.textSize = 9f * resources.displayMetrics.scaledDensity * labelScale
                labelOutline.strokeWidth = density * thicknessScale
                textPaint.getFontMetrics(fontMetrics)
                val metrics = fontMetrics
                val scaledPadding = padding * labelScale
                val labelHeight = metrics.descent - metrics.ascent + 2 * scaledPadding
                val distance = "${enemy.distanceMeters.roundToInt()}m"
                val name = enemy.name.ifBlank { "Unknown" }
                val nameWidth = if (drawName) textPaint.measureText(name) + 2 * scaledPadding else 0f
                val distanceWidth = if (drawDistance) textPaint.measureText(distance) + 2 * scaledPadding else 0f
                val gap = if (drawName && drawDistance) 2 * density * labelScale else 0f
                val rowWidth = nameWidth + gap + distanceWidth

                var lx = labelCenterX - rowWidth / 2
                var ly = labelTopY - labelHeight - 2 * density * labelScale
                lx = lx.coerceIn(0f, (width - rowWidth).coerceAtLeast(0f))
                ly = ly.coerceIn(0f, (height - labelHeight).coerceAtLeast(0f))

                for (attempt in 0..5) {
                    candidateLabel.set(lx, ly, lx + rowWidth, ly + labelHeight)
                    var overlaps = false
                    for (i in 0 until occupiedCount) {
                        if (RectF.intersects(labelBounds[i], candidateLabel)) { overlaps = true; break }
                    }
                    if (!overlaps) break
                    ly -= labelHeight + 2 * density
                    if (ly < 0f) { ly = (labelTopY + 3 * density).coerceAtMost(height - labelHeight); break }
                }

                if (occupiedCount < labelBounds.size)
                    labelBounds[occupiedCount++].set(lx, ly, lx + rowWidth, ly + labelHeight)
                if (drawName) {
                    rounded.set(lx, ly, lx + nameWidth, ly + labelHeight)
                    canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelBgPaint)
                    canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelOutline)
                    canvas.drawText(name, lx + scaledPadding, ly + scaledPadding - metrics.ascent, textPaint)
                }
                if (drawDistance) {
                    val distanceLeft = lx + nameWidth + gap
                    rounded.set(distanceLeft, ly, distanceLeft + distanceWidth, ly + labelHeight)
                    canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelBgPaint)
                    canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelOutline)
                    canvas.drawText(distance, distanceLeft + scaledPadding, ly + scaledPadding - metrics.ascent, textPaint)
                }
                if (notableValue) {
                    textPaint.color = savedTextColor
                    labelBgPaint.color = savedBgColor
                    labelOutline.color = savedOutlineColor
                }
                if (applyLootAlpha) {
                    textPaint.alpha = savedTextAlpha
                    labelBgPaint.alpha = savedBgAlpha
                    labelOutline.alpha = savedOutlineAlpha
                }
            }
        }
        if (data.nearbyContainerCount > 0 && containerGuideEnabled) {
            val count = data.nearbyContainerCount
            val countText = "$count container${if (count > 1) "s" else ""} nearby"
            val savedSize = textPaint.textSize
            val savedColor = textPaint.color
            textPaint.textSize = 10f * resources.displayMetrics.scaledDensity
            textPaint.getFontMetrics(fontMetrics)
            val tw = textPaint.measureText(countText) + 2 * padding
            val th = fontMetrics.descent - fontMetrics.ascent + 2 * padding
            val cx = width - tw - 8 * density
            val cy = 8 * density
            rounded.set(cx, cy, cx + tw, cy + th)
            canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelBgPaint)
            canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelOutline)
            textPaint.color = LootDisplayPolicy.lootDistanceColor(0.0)
            canvas.drawText(countText, cx + padding, cy + padding - fontMetrics.ascent, textPaint)
            textPaint.color = savedColor
            textPaint.textSize = savedSize
        }
    }
}
