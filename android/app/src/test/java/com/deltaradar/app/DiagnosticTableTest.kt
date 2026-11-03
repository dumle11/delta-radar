package com.deltaradar.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class DiagnosticTableTest {
    private val actor = DiagnosticActor(
        "0x1234", "player", "Name\nLine", "0x5678", "Character",
        "unknown", "alive*", "unverified"
    )
    private val report = DiagnosticReport(
        1_000, "world sampled", "0x10", "0x20", "0x30", "0x40",
        "20", 1, "0x50", 14, listOf(actor), 1
    )

    @Test fun formatsWorldActorAndFailureWithoutClaimingDecryption() {
        val text = DiagnosticTable.format(report, 2_000)
        assertTrue(text.contains("GWorld slot | 0x10"))
        assertTrue(text.contains("Actor array | 0x40 | count 20"))
        assertTrue(text.contains("last 0x50 (errno 14)"))
        assertTrue(text.contains("Name Line"))
        assertTrue(text.contains("unverified"))
        assertFalse(text.contains("Name\nLine"))
    }

    @Test fun distinguishesStaleAndUnavailableData() {
        assertTrue(DiagnosticTable.format(null, 0).contains("unavailable"))
        assertTrue(DiagnosticTable.format(report, 4_001).contains("STALE"))
        assertTrue(DiagnosticTable.format(report.copy(totalEntities = -1, actors = emptyList()), 1_000)
            .contains("Entity rows not included"))
        assertTrue(DiagnosticTable.format(report.copy(failureErrno = 0), 1_000).contains("short read"))
    }

    @Test fun limitsRows() {
        val text = DiagnosticTable.format(report.copy(
            actors = List(70) { actor }, totalEntities = 70
        ), 1_000)
        assertTrue(text.contains("Filtered published entities: 70; showing 64"))
        assertTrue(text.split("0x1234").size - 1 == 64)
    }
}
