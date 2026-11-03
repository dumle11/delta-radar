package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class OverlayLifecycleTest {
    @Test fun queuedProjectionCannotReappearAfterHideOrSubsequentShow() {
        val lifecycle = OverlayLifecycle()
        lifecycle.show()
        val pending = lifecycle.capture()!!
        assertTrue(lifecycle.accepts(pending))
        lifecycle.hide()
        assertNull(lifecycle.capture())
        assertFalse(lifecycle.accepts(pending))
        lifecycle.show()
        assertFalse(lifecycle.accepts(pending))
        assertTrue(lifecycle.accepts(lifecycle.capture()!!))
    }
    @Test fun destroyedServiceCannotPublishOrBeReshownByDelayedWork() {
        val lifecycle = OverlayLifecycle()
        lifecycle.show()
        val pending = lifecycle.capture()!!
        lifecycle.destroy()
        lifecycle.show()
        assertFalse(lifecycle.accepts(pending))
        assertNull(lifecycle.capture())
    }
}
