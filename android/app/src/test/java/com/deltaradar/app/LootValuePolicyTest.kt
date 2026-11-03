package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class LootValuePolicyTest {
    @Test fun strictIndividualThreshold() {
        assertFalse(LootValuePolicy.passes(true, 20_000, true, "shop-dynamic-guide", 20_000.0))
        assertTrue(LootValuePolicy.passes(true, 20_000, true, "shop-dynamic-guide", 20_001.0))
        assertFalse(LootValuePolicy.passes(true, 20_000, true, "shop-dynamic-guide", 2_798.0))
    }
    @Test fun unknownAndStaticPricesDoNotQualify() {
        assertFalse(LootValuePolicy.passes(true, 20_000, false, "shop-dynamic-guide", 90_000.0))
        assertFalse(LootValuePolicy.passes(true, 20_000, true, "initial-guide", 90_000.0))
        assertFalse(LootValuePolicy.passes(true, 20_000, true, "auction-cache", 90_000.0))
        assertFalse(LootValuePolicy.passes(true, 20_000, false, "", Double.NaN))
    }
    @Test fun allLootModePreservesUnknownContainers() {
        assertTrue(LootValuePolicy.passes(false, 20_000, false, "", Double.NaN))
    }
    @Test fun malformedValuesNeverQualify() {
        for (value in listOf(Double.NaN, Double.POSITIVE_INFINITY, -1.0, 20_001.5, 1_000_000_001.0))
            assertFalse(LootValuePolicy.passes(true, 20_000, true, "shop-dynamic-guide", value))
    }
    @Test fun fiftyThousandRemainsSelectable() {
        assertFalse(LootValuePolicy.passes(true, 50_000, true, "shop-dynamic-guide", 49_999.0))
        assertTrue(LootValuePolicy.passes(true, 50_000, true, "shop-dynamic-guide", 50_001.0))
        assertEquals("20,000", LootValuePolicy.label(20_000.0))
    }
}
