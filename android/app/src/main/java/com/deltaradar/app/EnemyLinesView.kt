package com.deltaradar.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.RectF
import android.os.SystemClock
import android.view.View
import java.util.Locale
import kotlin.math.roundToInt

class EnemyLinesView(context: Context, private val onExpired: () -> Unit) : View(context) {
    companion object { const val FRESHNESS_MS = 1200L }

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
    data class Frame(val me: Pair<Float, Float>?, val enemies: List<Enemy>, val warning: String? = null)

    private var frame: Frame? = null
    private var receivedAt = 0L
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
    private val warningTextPaint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        color = 0xFFFFD180.toInt()
        textSize = 11 * resources.displayMetrics.scaledDensity
        typeface = android.graphics.Typeface.create(android.graphics.Typeface.DEFAULT, android.graphics.Typeface.BOLD)
    }
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
    private val expire = Runnable { clear(); onExpired() }

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

    fun submit(value: Frame, receivedAtMs: Long) {
        if (value.me == null || (value.enemies.isEmpty() && value.warning == null)) { clear(); return }
        frame = value; receivedAt = receivedAtMs
        removeCallbacks(expire)
        visibility = VISIBLE
        invalidate()
        postDelayed(expire, (FRESHNESS_MS - (SystemClock.elapsedRealtime() - receivedAt)).coerceAtLeast(1))
    }

    fun clear() {
        removeCallbacks(expire); frame = null; visibility = GONE
    }

    override fun onDetachedFromWindow() { clear(); super.onDetachedFromWindow() }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val healthPosition = Settings.healthBarPosition(context)
        val healthThickness = Settings.healthBarThicknessDp(context) * density
        val data = frame ?: return
        if (SystemClock.elapsedRealtime() - receivedAt >= FRESHNESS_MS || width <= 0 || height <= 0) {
            post(expire); return
        }
        val me = data.me ?: return
        val padding = 2f * density
        val occupied = mutableListOf<RectF>()

        data.warning?.let { warning ->
            val left = 8f * density
            val top = 8f * density
            val inset = 6f * density
            val bounds = RectF(left, top, left + warningTextPaint.measureText(warning) + inset * 2,
                top + warningTextPaint.fontSpacing + inset * 2)
            canvas.drawRoundRect(bounds, 4f * density, 4f * density, labelBgPaint)
            canvas.drawText(warning, left + inset, top + inset - warningTextPaint.ascent(), warningTextPaint)
            occupied.add(bounds)
        }

        for (enemy in data.enemies) {
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
            val hasSkeleton = entityMode == Settings.DRAW_MODE_SKELETON &&
                enemy.boneParents != null && enemy.boneScreenX != null && enemy.boneScreenY != null
            if ((enemy.onScreen || hasSkeleton) && (drawBody || drawHealth)) {
                val headX = enemy.headX * width
                val headY = enemy.headY * height
                val feetX = enemy.feetX * width
                val feetY = enemy.feetY * height
                val boxH = kotlin.math.abs(feetY - headY).coerceAtLeast(1f)
                boxHeightForLabel = boxH
                val centerX = (headX + feetX) * 0.5f
                val top = minOf(headY, feetY)
                val bottom = top + boxH
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
                when (if (drawBody) entityMode else Settings.DRAW_MODE_OFF) {
                    Settings.DRAW_MODE_SKELETON -> {
                        val bx = enemy.boneScreenX
                        val by = enemy.boneScreenY
                        val parents = enemy.boneParents
                        if (bx != null && by != null && parents != null &&
                            bx.size == by.size && bx.size == parents.size) {
                            boxPaint.strokeWidth = density * thicknessScale
                            fun boneX(i: Int): Float = bx[i] * width
                            fun boneY(i: Int): Float = by[i] * height
                            for (child in 1 until parents.size) {
                                val parent = parents[child]
                                if (parent !in 0 until child || !bx[child].isFinite() || !by[child].isFinite() ||
                                    !bx[parent].isFinite() || !by[parent].isFinite()) continue
                                canvas.drawLine(boneX(parent), boneY(parent), boneX(child), boneY(child), boxShadowPaint)
                                canvas.drawLine(boneX(parent), boneY(parent), boneX(child), boneY(child), boxPaint)
                            }
                        }
                    }

                    Settings.DRAW_MODE_BOXES -> {
                        val boxW = boxH * when (enemy.kind) {
                            "vehicle" -> 0.9f
                            "loot" -> 0.65f
                            else -> 0.45f
                        }
                        val bounds = enemy.bodyBounds
                        val boxRect = if (bounds != null) RectF(bounds[0]*width,bounds[1]*height,bounds[2]*width,bounds[3]*height)
                            else RectF(centerX-boxW*0.5f,top,centerX+boxW*0.5f,bottom)
                        canvas.drawRect(boxRect, boxShadowPaint)
                        canvas.drawRect(boxRect, boxPaint)
                    }
                }
                if (drawHealth && enemy.onScreen && enemy.healthKnown && enemy.maxHealth > 0f) {
                    val hpFrac = (enemy.health / enemy.maxHealth).coerceIn(0f, 1f)
                    val bounds = enemy.bodyBounds
                    val bar = HealthBarLayout.calculate(
                        bounds?.get(0)?.times(width) ?: (centerX - healthBoxW * .5f),
                        bounds?.get(1)?.times(height) ?: top,
                        bounds?.get(2)?.times(width) ?: (centerX + healthBoxW * .5f),
                        bounds?.get(3)?.times(height) ?: bottom,
                        healthPosition, healthThickness,
                        2f * density, hpFrac)
                    if (bar != null) {
                        fun rect(r: HealthBarLayout.Rect) = RectF(r.left, r.top, r.right, r.bottom)
                        val background = rect(bar.background)
                        canvas.drawRect(background, healthBgPaint)
                        val r = ((1f - hpFrac) * 255).toInt().coerceIn(0, 255)
                        val g = (hpFrac * 255).toInt().coerceIn(0, 255)
                        healthFillPaint.color = Color.argb(220, r, g, 0)
                        canvas.drawRect(rect(bar.fill), healthFillPaint)
                        occupied.add(background)
                        labelTopY = minOf(labelTopY, background.top)
                    }
                }
            }

            if (drawName || drawDistance) {
                val labelScale = if (enemy.onScreen)
                    (boxHeightForLabel / (72f * density)).coerceIn(0.45f, 1f)
                else 0.6f
                textPaint.textSize = 9f * resources.displayMetrics.scaledDensity * labelScale
                labelOutline.strokeWidth = density * thicknessScale
                val metrics = textPaint.fontMetrics
                val scaledPadding = padding * labelScale
                val labelHeight = metrics.descent - metrics.ascent + 2 * scaledPadding
                val distance = if (enemy.distanceMeters < 100)
                    String.format(Locale.US, "%.0fm", enemy.distanceMeters)
                else "${enemy.distanceMeters.roundToInt()}m"
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
                    val candidate = RectF(lx, ly, lx + rowWidth, ly + labelHeight)
                    if (occupied.none { RectF.intersects(it, candidate) }) break
                    ly -= labelHeight + 2 * density
                    if (ly < 0f) { ly = (labelTopY + 3 * density).coerceAtMost(height - labelHeight); break }
                }

                val occupiedRect = RectF(lx, ly, lx + rowWidth, ly + labelHeight)
                occupied.add(occupiedRect)
                if (drawName) {
                    rounded.set(RectF(lx, ly, lx + nameWidth, ly + labelHeight))
                    canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelBgPaint)
                    canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelOutline)
                    canvas.drawText(name, lx + scaledPadding, ly + scaledPadding - metrics.ascent, textPaint)
                }
                if (drawDistance) {
                    val distanceLeft = lx + nameWidth + gap
                    rounded.set(RectF(distanceLeft, ly, distanceLeft + distanceWidth, ly + labelHeight))
                    canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelBgPaint)
                    canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelOutline)
                    canvas.drawText(distance, distanceLeft + scaledPadding, ly + scaledPadding - metrics.ascent, textPaint)
                }
            }
        }
    }
}
