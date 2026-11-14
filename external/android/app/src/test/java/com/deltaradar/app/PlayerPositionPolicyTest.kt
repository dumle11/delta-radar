package com.deltaradar.app

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class PlayerPositionPolicyTest {
    private fun hidden(
        kind: String = "player",
        plaintext: Boolean = true,
        seenFrame: Long = 7,
        currentFrame: Long = 7,
        cameraPlaintext: Boolean = true,
        cameraTimestamp: Double = 5.0,
        sampledAt: Long = 1_000,
        now: Long = 1_750
    ) = PlayerPositionPolicy.hidePlayer(
        kind, plaintext, seenFrame, currentFrame, cameraPlaintext, cameraTimestamp, sampledAt, now
    )

    @Test fun requiresCurrentPositionAndCameraChecksForPlayers() {
        assertFalse(hidden())
        assertTrue(hidden(plaintext = false))
        assertTrue(hidden(seenFrame = 6))
        assertTrue(hidden(seenFrame = -1))
        assertTrue(hidden(currentFrame = 0, seenFrame = 0))
        assertTrue(hidden(cameraPlaintext = false))
        assertTrue(hidden(cameraTimestamp = 0.0))
        assertTrue(hidden(cameraTimestamp = Double.NaN))
        assertTrue(hidden(sampledAt = 0))
        assertTrue(hidden(now = 1_751))
        assertTrue(hidden(now = 999))
    }

    @Test fun preservesBotsAndLoot() {
        assertFalse(hidden(kind = "ai", plaintext = false, cameraPlaintext = false))
        assertFalse(hidden(kind = "loot", plaintext = false, cameraPlaintext = false))
        assertFalse(PlayerPositionPolicy.useBones("player", false))
        assertTrue(PlayerPositionPolicy.useBones("player", true))
        assertTrue(PlayerPositionPolicy.useBones("ai", true))
        assertFalse(PlayerPositionPolicy.useBones("ai", false))
    }
}
