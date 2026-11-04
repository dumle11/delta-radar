package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class ScannerRecoveryBudgetTest {
    @Test fun repeatedExitsStopAfterThreeRecoveries() {
        val budget = ScannerRecoveryBudget()
        assertTrue(budget.allow(0))
        assertTrue(budget.allow(3_000))
        assertTrue(budget.allow(6_000))
        assertFalse(budget.allow(9_000))
        assertFalse(budget.allow(59_999))
    }

    @Test fun rollingWindowOnlyExpiresOldAttempts() {
        val budget = ScannerRecoveryBudget()
        assertTrue(budget.allow(0))
        assertTrue(budget.allow(20_000))
        assertTrue(budget.allow(40_000))
        assertTrue(budget.allow(60_000))
        assertFalse(budget.allow(60_001))
        assertTrue(budget.allow(80_000))
    }

    @Test fun explicitRetryStartsWithANewBudget() {
        val budget = ScannerRecoveryBudget()
        repeat(3) { assertTrue(budget.allow(0)) }
        assertFalse(budget.allow(0))
        budget.reset()
        assertTrue(budget.allow(1))
    }
}
