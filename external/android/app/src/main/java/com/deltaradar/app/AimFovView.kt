package com.deltaradar.app

import android.content.Context
import android.graphics.Canvas
import android.graphics.Paint
import android.view.View

class AimFovView(context: Context) : View(context) {
    var radiusPercent = 15
        set(value) { field = value; invalidate() }
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeWidth = resources.displayMetrics.density
        color = 0xCCFFFFFF.toInt()
    }
    init {
        visibility = GONE
        importantForAccessibility = IMPORTANT_FOR_ACCESSIBILITY_NO
    }
    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        canvas.drawCircle(width / 2f, height / 2f,
            AimMath.radius(width, height, radiusPercent), paint)
    }
}
