package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class LootDisplayPolicyTest {
    private fun select(container: Boolean, guide: Boolean = true, containerLines: Boolean = false,
                       contentsKnown: Boolean = false, features: Int = 0,
                       filter: Boolean = true, known: Boolean = false,
                       value: Double = Double.NaN) = LootDisplayPolicy.select(
        container, guide, containerLines, contentsKnown, features, filter, 20_000, known, "shop-dynamic-guide", value)

    @Test fun unopenedContainersHaveGuidesWithLooseLootDisabled() {
        val display = select(container = true)!!
        assertTrue(display.containerGuide)
        assertEquals(LootDisplayPolicy.CONTAINER_FEATURES_BASE, display.features)
        assertEquals(0, display.features and Settings.FEATURE_LINES)
        assertTrue(display.features and Settings.FEATURE_BODY != 0)
        assertTrue(display.features and Settings.FEATURE_NAMES != 0)
        assertTrue(display.features and Settings.FEATURE_DISTANCE != 0)
        assertEquals(0, display.features and Settings.FEATURE_HEALTH)
    }

    @Test fun containerLinesEnabledAddsTracerFeature() {
        val display = select(container = true, containerLines = true)!!
        assertTrue(display.containerGuide)
        assertTrue(display.features and Settings.FEATURE_LINES != 0)
        assertTrue(display.features and Settings.FEATURE_BODY != 0)
    }

    @Test fun lootedContainersDisappear() {
        assertNull(select(container = true, contentsKnown = true))
    }

    @Test fun lootedContainerWithValueShowsAsNormalLoot() {
        val display = select(container = true, contentsKnown = true,
            features = Settings.ALL_FEATURES, known = true, value = 90_000.0)!!
        assertFalse(display.containerGuide)
        assertEquals(Settings.ALL_FEATURES, display.features)
    }

    @Test fun containerLocationsDoNotDependOnContentsValue() {
        val unopened = select(container = true)
        assertEquals(unopened, select(container = true, known = true, value = 100.0))
        assertEquals(unopened, select(container = true, known = true, value = 90_000.0))
    }

    @Test fun ordinaryLootStillRequiresEnabledAndQualifyingValue() {
        assertNull(select(container = false))
        assertNull(select(container = false, known = true, value = 90_000.0))
        assertNull(select(container = false, features = Settings.ALL_FEATURES))
        assertNull(select(container = false, features = Settings.ALL_FEATURES, known = true, value = 100.0))
        val valuable = select(container = false, features = Settings.ALL_FEATURES, known = true, value = 90_000.0)!!
        assertFalse(valuable.containerGuide)
        assertEquals(Settings.ALL_FEATURES, valuable.features)
    }

    @Test fun turningGuideOffRestoresExistingLootRules() {
        assertNull(select(container = true, guide = false, features = Settings.ALL_FEATURES))
        assertNull(select(container = true, guide = false, known = true, value = 90_000.0))
        assertFalse(select(container = true, guide = false, features = Settings.ALL_FEATURES,
            known = true, value = 90_000.0)!!.containerGuide)
        assertNotNull(select(container = true, guide = false, features = Settings.ALL_FEATURES, filter = false))
    }

    @Test fun lootDistanceColorGreenWhenClose() {
        val green = LootDisplayPolicy.lootDistanceColor(0.0)
        assertEquals(0xFF, green ushr 24 and 0xFF)
        assertTrue(green ushr 8 and 0xFF > 0xA0)
    }

    @Test fun lootDistanceColorRedWhenFar() {
        val red = LootDisplayPolicy.lootDistanceColor(200.0)
        assertTrue(red ushr 16 and 0xFF > 0xA0)
    }

    @Test fun lootDistanceColorClampedAtMax() {
        val atMax = LootDisplayPolicy.lootDistanceColor(200.0)
        val pastMax = LootDisplayPolicy.lootDistanceColor(500.0)
        assertEquals(atMax, pastMax)
    }

    @Test fun applyOpacityHalvesAlpha() {
        val full = 0xFFFF0000.toInt()
        val half = LootDisplayPolicy.applyOpacity(full, 50)
        assertEquals(127, half ushr 24 and 0xFF)
        assertEquals(0xFF, half ushr 16 and 0xFF)
    }
}
