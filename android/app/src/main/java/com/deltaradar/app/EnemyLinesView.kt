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
        val team: Int = 2
    )
    data class Frame(val me: Pair<Float, Float>?, val enemies: List<Enemy>)

    @Volatile var drawnFrames = 0L
        private set
    private var frame: Frame? = null
    private var expiresAt = 0L
    private var sourceReceivedAt = 0L
    private var playerFeatures = 0
    private var botFeatures = 0
    private var teammateFeatures = 0
    private var lootFeatures = 0
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
                 pMode: Int, bMode: Int, tMode: Int, lMode: Int, thickness: Float) {
        if (playerFeatures == pFeatures && botFeatures == bFeatures && teammateFeatures == tFeatures &&
            lootFeatures == lFeatures && playerColor == pColor && botColor == bColor &&
            teammateColor == tColor && lootColor == lColor &&
            playerDrawMode == pMode && botDrawMode == bMode && teammateDrawMode == tMode &&
            lootDrawMode == lMode &&
            thicknessScale == thickness) return
        playerFeatures = pFeatures; botFeatures = bFeatures; teammateFeatures = tFeatures; lootFeatures = lFeatures
        playerColor = pColor; botColor = bColor; teammateColor = tColor; lootColor = lColor
        playerDrawMode = pMode; botDrawMode = bMode; teammateDrawMode = tMode; lootDrawMode = lMode
        thicknessScale = thickness.coerceIn(0.25f, 3f)
        if (pFeatures == 0 && bFeatures == 0 && tFeatures == 0 && lFeatures == 0) clear()
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
        for (enemy in data.enemies) {
            // Tracers, labels and boxes must use the same camera sample.
            // Blending screen coordinates adds lag during every camera turn.
            val ex = enemy.screenX * width
            val ey = enemy.screenY * height
            val mx = me.first * width
            val my = me.second * height
            val isLoot = enemy.kind == "loot" || enemy.kind == "objective"
            val isPlayer = enemy.kind == "player"
            val entityColor = when {
                isLoot -> lootColor
                enemy.team == 1 -> teammateColor
                isPlayer -> playerColor
                else -> botColor
            }
            val entityMode = when {
                isLoot -> lootDrawMode
                enemy.team == 1 -> teammateDrawMode
                isPlayer -> playerDrawMode
                else -> botDrawMode
            }
            val features = when {
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

            if (drawTracer) {
                linePaint.color = 0xD5090D16.toInt()
                linePaint.strokeWidth = 3f * density * thicknessScale
                canvas.drawLine(mx, my, ex, ey, linePaint)
                linePaint.color = entityColor
                linePaint.strokeWidth = density * thicknessScale
                canvas.drawLine(mx, my, ex, ey, linePaint)
            }

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
                val top = body?.get(1)?.times(height) ?: minOf(headY, feetY)
                val bottom = body?.get(3)?.times(height) ?: maxOf(headY, feetY)
                val boxH = (bottom - top).coerceAtLeast(1f)
                boxHeightForLabel = boxH
                val centerX = body?.let { (it[0] + it[2]) * width * .5f } ?: ((headX + feetX) * .5f)
                labelCenterX = centerX
                labelTopY = top

                boxPaint.color = entityColor
                boxPaint.strokeWidth = density * thicknessScale
                boxShadowPaint.strokeWidth = 2.5f * density * thicknessScale
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
                        val boxW = boxH * when (enemy.kind) {
                            "vehicle" -> 0.9f
                            "loot" -> 0.65f
                            else -> 0.45f
                        }
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

            if (drawName || drawDistance) {
                val labelScale = if (enemy.onScreen)
                    (boxHeightForLabel / (72f * density)).coerceIn(0.45f, 1f)
                else 0.6f
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
            }
        }
    }
}
