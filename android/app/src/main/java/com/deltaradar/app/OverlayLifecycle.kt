package com.deltaradar.app

/** Invalidates queued work across Hide, Show and destruction. */
class OverlayLifecycle {
    private var generation = 0L
    private var visible = false
    private var destroyed = false

    @Synchronized fun show() { if (!destroyed) { generation++; visible = true } }
    @Synchronized fun hide() { generation++; visible = false }
    @Synchronized fun destroy() { hide(); destroyed = true }
    @Synchronized fun capture(): Long? = if (visible && !destroyed) generation else null
    @Synchronized fun accepts(token: Long): Boolean = !destroyed && visible && generation == token
}
