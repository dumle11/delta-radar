package com.deltaradar.app

import org.junit.Assert.assertEquals
import org.junit.Test

class ScannerConfigTest {
    @Test
    fun readerUsesTheVerifiedAssetAndTransport() {
        assertEquals("memscan_legacy", ScannerConfig.ASSET)
        assertEquals("/data/local/tmp/delta_radar_legacy/memscan", ScannerConfig.PATH)
        assertEquals("run vmreadv", ScannerConfig.ARGUMENTS)
    }
}
