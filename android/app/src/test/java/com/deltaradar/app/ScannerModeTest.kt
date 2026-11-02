package com.deltaradar.app

import org.junit.Assert.assertEquals
import org.junit.Test

class ScannerModeTest {
    @Test
    fun readersUseTheirIntendedAssetsAndTransports() {
        assertEquals("ue_telemetry", ScannerMode.CURRENT.asset)
        assertEquals("/data/local/tmp/ue_telemetry", ScannerMode.CURRENT.path)
        assertEquals("run", ScannerMode.CURRENT.arguments)
        assertEquals("memscan_legacy", ScannerMode.LEGACY.asset)
        assertEquals("/data/local/tmp/delta_radar_legacy/memscan", ScannerMode.LEGACY.path)
        assertEquals("run vmreadv", ScannerMode.LEGACY.arguments)
    }
}
