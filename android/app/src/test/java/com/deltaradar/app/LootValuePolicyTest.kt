package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class LootValuePolicyTest {
    @Test fun strictIndividualThreshold() {
        assertFalse(LootValuePolicy.passes(true, 20_000, true, "auction-cache", 20_000.0))
        assertTrue(LootValuePolicy.passes(true, 20_000, true, "auction-cache", 20_001.0))
        assertFalse(LootValuePolicy.passes(true, 20_000, true, "auction-cache", 2_798.0))
    }
    @Test fun unknownAndStaticPricesDoNotQualify() {
        assertFalse(LootValuePolicy.passes(true, 20_000, false, "auction-cache", 90_000.0))
        assertFalse(LootValuePolicy.passes(true, 20_000, true, "initial-guide", 90_000.0))
        assertFalse(LootValuePolicy.passes(true, 20_000, false, "", Double.NaN))
    }
    @Test fun allLootModePreservesUnknownContainers() {
        assertTrue(LootValuePolicy.passes(false, 20_000, false, "", Double.NaN))
    }
    @Test fun malformedValuesNeverQualify() {
        for (value in listOf(Double.NaN, Double.POSITIVE_INFINITY, -1.0, 20_001.5, 1_000_000_001.0))
            assertFalse(LootValuePolicy.passes(true, 20_000, true, "auction-cache", value))
    }
    @Test fun fiftyThousandRemainsSelectable() {
        assertFalse(LootValuePolicy.passes(true, 50_000, true, "auction-cache", 49_999.0))
        assertTrue(LootValuePolicy.passes(true, 50_000, true, "auction-cache", 50_001.0))
        assertEquals("20,000", LootValuePolicy.label(20_000.0))
    }
}
