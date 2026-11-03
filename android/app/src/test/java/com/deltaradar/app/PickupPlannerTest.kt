package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test
import com.deltaradar.app.PickupPlanner.Decision
import com.deltaradar.app.PickupPlanner.Grid
import com.deltaradar.app.PickupPlanner.Item
import com.deltaradar.app.PickupPlanner.Placement
import com.deltaradar.app.PickupPlanner.Session
import com.deltaradar.app.PickupPlanner.Snapshot
import com.deltaradar.app.PickupPlanner.StopReason

class PickupPlannerTest {
    private fun item(gid: String, value: Long = 60_000, width: Int = 1, height: Int = 1) =
        Item(gid, 15080050015, 1, width, height, value, "shop-dynamic-guide", searched = true)
    private fun snapshot(items: List<Item>, grids: List<Grid> = listOf(Grid("bag", 3, 3, emptySet()))) =
        Snapshot("match-a", "safe-a", "window-a", 1, 100, true, true, true, items, emptyList(), grids)
    private fun take(d: Decision) = (d as Decision.Take).candidate

    @Test fun individualValueHasPriorityOverStackOrDensity() {
        val s = snapshot(listOf(item("small", 50_000), item("large", 80_000, 2, 2),
            item("stack", 900_000).copy(count = 2, maxStack = 2)))
        assertEquals("large", take(PickupPlanner.next(s, 110, 20_000)).item.gid)
    }

    @Test fun onlySearchedSupportedPricesAboveThresholdQualify() {
        val s = snapshot(listOf(item("equal", 20_000), item("unknown").copy(unitValue = null),
            item("static").copy(valueSource = "initial-guide"), item("key").copy(catalogId = 15050010001),
            item("gear").copy(catalogId = 11050003001), item("searching").copy(searched = false),
            item("good", 20_001)))
        assertEquals("good", take(PickupPlanner.next(s, 110, 20_000)).item.gid)
    }

    @Test fun rejectsStaleFutureIncompleteAndClosedContexts() {
        val s = snapshot(listOf(item("a")))
        for (bad in listOf(s.copy(capturedAtMs = 0), s.copy(capturedAtMs = 401),
            s.copy(sourceComplete = false), s.copy(storageComplete = false), s.copy(windowOpen = false)))
            assertEquals(Decision.Stop(StopReason.INVALID_CONTEXT), PickupPlanner.next(bad, 400, 0))
    }

    @Test fun fragmentedFreeCellsDoNotImplyAnItemFits() {
        val grid = Grid("bag", 3, 3, setOf(1, 4, 7))
        val s = snapshot(listOf(item("expensive", 90_000, 2, 2), item("fits", 60_000)), listOf(grid))
        assertEquals("fits", take(PickupPlanner.next(s, 110, 0)).item.gid)
    }

    @Test fun allowsRotationAndUsesRigWhenBagIsFull() {
        val s = snapshot(listOf(item("a", width = 2, height = 1)), listOf(
            Grid("bag", 1, 1, setOf(0)), Grid("rig", 1, 2, emptySet())))
        assertEquals(Placement("rig", 0, 0, true), take(PickupPlanner.next(s, 110, 0)).placement)
    }

    @Test fun fullStorageStopsWithoutDropOrSwap() {
        val s = snapshot(listOf(item("a")), listOf(Grid("bag", 1, 1, setOf(0))))
        assertEquals(Decision.Stop(StopReason.NO_ELIGIBLE_ITEM), PickupPlanner.next(s, 110, 0))
    }

    @Test fun rejectsDuplicateIdentityOrMalformedOccupancy() {
        val s = snapshot(listOf(item("a")))
        for (bad in listOf(s.copy(items = s.items + s.items), s.copy(carried = s.items),
            s.copy(grids = listOf(Grid("bag", 1, 1, setOf(-1)))),
            s.copy(grids = listOf(Grid("bag", 1, 1, setOf(1)))),
            s.copy(items = listOf(item("a").copy(width = 0)))))
            assertEquals(Decision.Stop(StopReason.INVALID_CONTEXT), PickupPlanner.next(bad, 110, 0))
    }

    @Test fun missingSourceIsNotTransferConfirmation() {
        val session = Session(0)
        val s = snapshot(listOf(item("a"), item("b", 30_000)))
        assertEquals("a", take(session.advance(s, 110)).item.gid)
        assertEquals(Decision.Wait, session.advance(s.copy(sequence = 2, capturedAtMs = 200,
            items = listOf(item("b", 30_000))), 210))
    }

    @Test fun matchingCarriedIdentityUnlocksNextPickupFromFreshSpace() {
        val session = Session(0)
        val s = snapshot(listOf(item("a"), item("b", 30_000)))
        session.advance(s, 110)
        val after = s.copy(sequence = 2, capturedAtMs = 200, items = listOf(item("b", 30_000)),
            carried = listOf(item("a")), grids = listOf(Grid("bag", 3, 3, setOf(0))))
        val next = take(session.advance(after, 210))
        assertEquals("b", next.item.gid)
        assertEquals(Placement("bag", 1, 0, false), next.placement)
    }

    @Test fun ambiguousOrTimedOutTransferNeverIssuesAnotherAction() {
        val session = Session(0, 100)
        val s = snapshot(listOf(item("a")))
        session.advance(s, 110)
        val after = s.copy(sequence = 2, capturedAtMs = 200, items = emptyList(),
            carried = listOf(item("a").copy(catalogId = 15080050011)))
        assertEquals(Decision.Stop(StopReason.TRANSFER_UNCONFIRMED), session.advance(after, 210))
        assertEquals(Decision.Stop(StopReason.TRANSFER_UNCONFIRMED), session.advance(s.copy(capturedAtMs = 220), 230))
    }

    @Test fun changingContainerOrSessionStopsAndRequiresNewButtonPress() {
        val s = snapshot(listOf(item("a")))
        for (changed in listOf(s.copy(container = "safe-b"), s.copy(session = "match-b"), s.copy(window = "reopened"))) {
            val session = Session(0); session.advance(s, 110)
            assertEquals(Decision.Stop(StopReason.CONTEXT_CHANGED), session.advance(changed, 120))
        }
    }

    @Test fun cancelledOrBackwardsSnapshotsCannotResume() {
        val s = snapshot(listOf(item("a")))
        val session = Session(0); session.advance(s, 110); session.cancel()
        assertEquals(Decision.Stop(StopReason.CANCELLED), session.advance(s, 120))
        val second = Session(0); second.advance(s, 110)
        assertEquals(Decision.Stop(StopReason.INVALID_CONTEXT), second.advance(s.copy(sequence = 0), 120))
    }

    @Test fun confirmationMustHaveBeenCapturedAfterCommandWasIssued() {
        val s = snapshot(listOf(item("a"), item("b", 30_000)))
        val session = Session(0); session.advance(s, 150)
        val beforeCommand = s.copy(sequence = 2, capturedAtMs = 140,
            items = listOf(item("b", 30_000)), carried = listOf(item("a")))
        assertEquals(Decision.Wait, session.advance(beforeCommand, 160))
    }

    @Test fun outOfOrderFrameAfterWaitingIsRejected() {
        val s = snapshot(listOf(item("a")))
        val session = Session(0); session.advance(s, 110)
        assertEquals(Decision.Wait, session.advance(s.copy(sequence = 3, capturedAtMs = 200), 210))
        assertEquals(Decision.Stop(StopReason.INVALID_CONTEXT),
            session.advance(s.copy(sequence = 2, capturedAtMs = 190), 220))
    }

    @Test fun lateConfirmationCannotRestartAnExpiredSession() {
        val s = snapshot(listOf(item("a"), item("b", 30_000)))
        val session = Session(0, 100); session.advance(s, 110)
        val after = s.copy(sequence = 2, capturedAtMs = 200,
            items = listOf(item("b", 30_000)), carried = listOf(item("a")))
        assertEquals(Decision.Stop(StopReason.TRANSFER_UNCONFIRMED), session.advance(after, 210))
    }

    @Test fun freshnessBoundaryAndFutureClockAreStrict() {
        val s = snapshot(listOf(item("a")))
        assertTrue(PickupPlanner.next(s, 399, 0) is Decision.Take)
        assertEquals(Decision.Stop(StopReason.INVALID_CONTEXT), PickupPlanner.next(s, 400, 0))
        assertEquals(Decision.Stop(StopReason.INVALID_CONTEXT), PickupPlanner.next(s, 99, 0))
    }
}
