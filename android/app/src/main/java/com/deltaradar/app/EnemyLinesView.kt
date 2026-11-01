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
        val team: Int = 2
    )
    data class Frame(val me: Pair<Float, Float>?, val enemies: List<Enemy>)

    private var frame: Frame? = null
    private var receivedAt = 0L
    private var transitionStartedAt = 0L
    private var previousById = emptyMap<String, Enemy>()
    private var drawLines = false
    private var drawLabels = false
    private var playerColor = Settings.DEFAULT_PLAYER_COLOR
    private var botColor = Settings.DEFAULT_BOT_COLOR
    private var teammateColor = Settings.DEFAULT_TEAMMATE_COLOR
    private var playerDrawMode = Settings.DRAW_MODE_BOXES
    private var botDrawMode = Settings.DRAW_MODE_BOXES
    private var teammateDrawMode = Settings.DRAW_MODE_BOXES
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
    private val expire = Runnable { clear(); onExpired() }

    init {
        visibility = GONE
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
        isClickable = false; isFocusable = false
    }

    fun setStyle(lines: Boolean, labels: Boolean,
                 pColor: Int, bColor: Int, tColor: Int,
                 pMode: Int, bMode: Int, tMode: Int, thickness: Float) {
        if (drawLines == lines && drawLabels == labels &&
            playerColor == pColor && botColor == bColor && teammateColor == tColor &&
            playerDrawMode == pMode && botDrawMode == bMode && teammateDrawMode == tMode &&
            thicknessScale == thickness) return
        drawLines = lines; drawLabels = labels
        playerColor = pColor; botColor = bColor; teammateColor = tColor
        playerDrawMode = pMode; botDrawMode = bMode; teammateDrawMode = tMode
        thicknessScale = thickness.coerceIn(0.25f, 3f)
        if (!lines && !labels) clear() else if (frame != null) invalidate()
    }

    fun submit(value: Frame, receivedAtMs: Long) {
        if (value.me == null || value.enemies.isEmpty()) { clear(); return }
        previousById = frame?.enemies?.associateBy { it.id }.orEmpty()
        frame = value; receivedAt = receivedAtMs
        transitionStartedAt = SystemClock.elapsedRealtime()
        removeCallbacks(expire)
        visibility = VISIBLE
        invalidate()
        postDelayed(expire, (FRESHNESS_MS - (SystemClock.elapsedRealtime() - receivedAt)).coerceAtLeast(1))
    }

    fun clear() {
        removeCallbacks(expire); frame = null; previousById = emptyMap(); visibility = GONE
    }

    override fun onDetachedFromWindow() { clear(); super.onDetachedFromWindow() }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val data = frame ?: return
        if (!drawLines && !drawLabels) return
        if (SystemClock.elapsedRealtime() - receivedAt >= FRESHNESS_MS || width <= 0 || height <= 0) {
            post(expire); return
        }
        val me = data.me ?: return
        val padding = 2f * density
        val occupied = mutableListOf<RectF>()
        val blend = ((SystemClock.elapsedRealtime() - transitionStartedAt) / 50f).coerceIn(0f, 1f)

        for (enemy in data.enemies) {
            val previous = previousById[enemy.id]
            fun lerp(from: Float, to: Float): Float = from + (to - from) * blend
            val ex = lerp(previous?.screenX ?: enemy.screenX, enemy.screenX) * width
            val ey = lerp(previous?.screenY ?: enemy.screenY, enemy.screenY) * height
            val mx = me.first * width
            val my = me.second * height
            val isLoot = enemy.kind == "loot" || enemy.kind == "objective"
            val isPlayer = enemy.kind == "player"
            val entityColor = when {
                isLoot -> Settings.LOOT_COLOR
                enemy.team == 1 -> teammateColor
                isPlayer -> playerColor
                else -> botColor
            }
            val entityMode = when {
                isLoot -> Settings.DRAW_MODE_BOXES
                enemy.team == 1 -> teammateDrawMode
                isPlayer -> playerDrawMode
                else -> botDrawMode
            }
            if (entityMode == Settings.DRAW_MODE_OFF && !drawLabels) continue
            var labelCenterX = ex
            var labelTopY = ey

            if (drawLines && entityMode != Settings.DRAW_MODE_OFF) {
                linePaint.color = 0xD5090D16.toInt()
                linePaint.strokeWidth = 3f * density * thicknessScale
                canvas.drawLine(mx, my, ex, ey, linePaint)
                linePaint.color = entityColor
                linePaint.strokeWidth = density * thicknessScale
                canvas.drawLine(mx, my, ex, ey, linePaint)
            }

            var boxHeightForLabel = 48f * density
            if (enemy.onScreen && entityMode > Settings.DRAW_MODE_LINES) {
                val headX = lerp(previous?.headX ?: enemy.headX, enemy.headX) * width
                val headY = lerp(previous?.headY ?: enemy.headY, enemy.headY) * height
                val feetX = lerp(previous?.feetX ?: enemy.feetX, enemy.feetX) * width
                val feetY = lerp(previous?.feetY ?: enemy.feetY, enemy.feetY) * height
                val boxH = kotlin.math.abs(feetY - headY).coerceIn(7 * density, 180 * density)
                boxHeightForLabel = boxH
                val centerX = (headX + feetX) * 0.5f
                val top = minOf(headY, feetY)
                val bottom = top + boxH
                labelCenterX = centerX
                labelTopY = top

                boxPaint.color = entityColor
                boxPaint.strokeWidth = density * thicknessScale
                boxShadowPaint.strokeWidth = 2.5f * density * thicknessScale
                when (entityMode) {
                    Settings.DRAW_MODE_SKELETON -> {
                        val bx = enemy.boneScreenX
                        val by = enemy.boneScreenY
                        if (bx != null && by != null && bx.size >= 16) {
                            boxPaint.strokeWidth = density * thicknessScale
                            val shadowPaint = Paint(boxPaint).apply {
                                color = 0xAA000000.toInt(); strokeWidth = 2.5f * density * thicknessScale
                            }
                            /* Bone indices: 0=head 1=neck 2=spine_upper 3=pelvis
                             * 4=l_shoulder 5=l_elbow 6=l_hand 7=r_shoulder 8=r_elbow 9=r_hand
                             * 10=l_hip 11=l_knee 12=l_foot 13=r_hip 14=r_knee 15=r_foot */
                            val connections = intArrayOf(
                                0,1, 1,2, 2,3,
                                1,4, 4,5, 5,6,
                                1,7, 7,8, 8,9,
                                3,10, 10,11, 11,12,
                                3,13, 13,14, 14,15
                            )
                            for (c in connections.indices step 2) {
                                val a = connections[c]; val b = connections[c+1]
                                val ax = bx[a] * width; val ay = by[a] * height
                                val bxp = bx[b] * width; val byp = by[b] * height
                                canvas.drawLine(ax, ay, bxp, byp, shadowPaint)
                                canvas.drawLine(ax, ay, bxp, byp, boxPaint)
                            }
                            val headR = boxH * 0.07f
                            canvas.drawCircle(bx[0] * width, by[0] * height, headR, boxPaint)
                        } else {
                            val hw = boxH * 0.18f
                            val headR = boxH * 0.09f
                            val neckY = top + boxH * 0.13f
                            val shoulderY = top + boxH * 0.22f
                            val hipY = top + boxH * 0.55f
                            boxPaint.strokeWidth = density * thicknessScale
                            canvas.drawCircle(centerX, top + headR, headR, boxPaint)
                            canvas.drawLine(centerX, neckY, centerX, hipY, boxPaint)
                            canvas.drawLine(centerX - hw, shoulderY, centerX + hw, shoulderY, boxPaint)
                            canvas.drawLine(centerX - hw, shoulderY, centerX - hw * 1.1f, hipY, boxPaint)
                            canvas.drawLine(centerX + hw, shoulderY, centerX + hw * 1.1f, hipY, boxPaint)
                            canvas.drawLine(centerX, hipY, centerX - hw, bottom, boxPaint)
                            canvas.drawLine(centerX, hipY, centerX + hw, bottom, boxPaint)
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
                        val boxRect = RectF(left, top, right, bottom)
                        canvas.drawRect(boxRect, boxShadowPaint)
                        canvas.drawRect(boxRect, boxPaint)

                        if (enemy.healthKnown && enemy.maxHealth > 0f) {
                            val hpFrac = (enemy.health / enemy.maxHealth).coerceIn(0f, 1f)
                            val barW = 1.5f * density
                            val barGap = 2f * density
                            val barLeft = left - barW - barGap
                            val barRect = RectF(barLeft, top, barLeft + barW, bottom)
                            canvas.drawRect(barRect, healthBgPaint)
                            val fillTop = top + (bottom - top) * (1f - hpFrac)
                            val r = ((1f - hpFrac) * 255).toInt().coerceIn(0, 255)
                            val g = (hpFrac * 255).toInt().coerceIn(0, 255)
                            healthFillPaint.color = Color.argb(220, r, g, 0)
                            canvas.drawRect(RectF(barLeft, fillTop, barLeft + barW, bottom), healthFillPaint)
                        }
                    }
                }
            }

            if (drawLabels) {
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
                val nameWidth = textPaint.measureText(name) + 2 * scaledPadding
                val distanceWidth = textPaint.measureText(distance) + 2 * scaledPadding
                val gap = 2 * density * labelScale
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
                val nameRect = RectF(lx, ly, lx + nameWidth, ly + labelHeight)
                rounded.set(nameRect)
                canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelBgPaint)
                canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelOutline)
                canvas.drawText(name, lx + scaledPadding, ly + scaledPadding - metrics.ascent, textPaint)

                val distanceLeft = lx + nameWidth + gap
                val distanceRect = RectF(distanceLeft, ly, distanceLeft + distanceWidth, ly + labelHeight)
                rounded.set(distanceRect)
                canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelBgPaint)
                canvas.drawRoundRect(rounded, 3 * density, 3 * density, labelOutline)
                canvas.drawText(distance, distanceLeft + scaledPadding, ly + scaledPadding - metrics.ascent, textPaint)
            }
        }
        if (blend < 1f) postInvalidateOnAnimation()
    }
}
