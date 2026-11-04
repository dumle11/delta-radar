package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class ScannerTransportHealthTest {
    @Test fun onlyFreshTrafficFromTheVerifiedPidCanReplaceARootProbe() {
        assertTrue(ScannerTransportHealth.isFresh(42, 42, 1000, 1000))
        assertTrue(ScannerTransportHealth.isFresh(42, 42, 1000, 2999))
        assertFalse(ScannerTransportHealth.isFresh(42, 42, 1000, 3000))
        assertFalse(ScannerTransportHealth.isFresh(42, 43, 1000, 1000))
        assertFalse(ScannerTransportHealth.isFresh(42, null, 1000, 1000))
        assertFalse(ScannerTransportHealth.isFresh(0, 0, 1000, 1000))
        assertFalse(ScannerTransportHealth.isFresh(42, 42, -1, 1000))
        assertFalse(ScannerTransportHealth.isFresh(42, 42, 1001, 1000))
    }

    @Test fun disconnectOrSilenceRequiresVerificationAgain() {
        assertTrue(ScannerTransportHealth.isFresh(42, 42, 1000, 1100))
        assertFalse(ScannerTransportHealth.isFresh(42, null, 1000, 1101))
        assertFalse(ScannerTransportHealth.isFresh(42, 42, 1000, 4000))
        assertFalse(ScannerTransportHealth.isFresh(43, 42, 1000, 1100))
        assertTrue(ScannerTransportHealth.isFresh(43, 43, 1100, 1101))
    }
}
