package com.deltaradar.app

import com.deltaradar.app.PickupPlanner.Grid
import com.deltaradar.app.PickupPlanner.Item
import com.deltaradar.app.PickupPlanner.Placement
import com.deltaradar.app.PickupPlanner.Snapshot
import org.junit.Assert.*
import org.junit.Test

class BackpackReplacementPlannerTest {
    private fun item(gid: String, value: Long = 1_000, w: Int = 1, h: Int = 1) =
        Item(gid, 15080050015, 1, w, h, value, "shop-dynamic-guide", searched = true)
    private val cheap = listOf(item("a"), item("b", 2_000), item("c", 3_000), item("d", 9_000))
    private val places = cheap.mapIndexed { i, it -> it.gid to Placement("bag", i % 2, i / 2, false) }.toMap()
    private fun snapshot(incoming: List<Item> = listOf(item("new", 10_000)), carried: List<Item> = cheap,
                         grids: List<Grid> = listOf(Grid("bag", 2, 2, setOf(0, 1, 2, 3)))) =
        Snapshot("match", "container", "window", 1, 100, true, true, true, incoming, carried, grids)
    private fun plan(s: Snapshot, locations: Map<String, Placement> = places, bags: Set<String> = setOf("bag"),
                     protected: Set<String> = emptySet()) =
        BackpackReplacementPlanner.plan(s, locations, bags, 110, 0, protected)
    private fun ready(result: BackpackReplacementPlanner.Result) =
        (result as BackpackReplacementPlanner.Result.Ready).plan

    @Test fun replacesCheapestItemWhenFull() {
        val p = ready(plan(snapshot()))
        assertEquals(listOf("a"), p.drops.map { it.gid }); assertEquals(9_000L, p.valueGain)
        assertEquals(Placement("bag", 0, 0, false), p.placement)
    }
    @Test fun takesIntoFreeSpaceWithoutDroppingAnything() {
        val s = snapshot(carried = cheap.take(3), grids = listOf(Grid("bag", 2, 2, setOf(0, 1, 2))))
        assertTrue(ready(plan(s, places.filterKeys { it != "d" })).drops.isEmpty())
    }
    @Test fun makesSpaceForLargerItemBeforeAnyRemoval() {
        val p = ready(plan(snapshot(incoming = listOf(item("new", 10_000, 2, 1)))))
        assertEquals(listOf("a", "b"), p.drops.map { it.gid }); assertEquals(3_000L, p.lostValue)
        assertEquals(7_000L, p.valueGain)
    }
    @Test fun totalValueOfRemovedItemsMustBeStrictlyLower() {
        for (value in listOf(2_999L, 3_000L))
            assertEquals(BackpackReplacementPlanner.Result.NoUpgrade,
                plan(snapshot(incoming = listOf(item("new", value, 2, 1)))))
    }
    @Test fun unknownUnsupportedAndExplicitlyProtectedItemsAreKept() {
        val allProtected = setOf("b", "c", "d")
        for (a in listOf(item("a").copy(unitValue = null), item("a").copy(catalogId = 15050010001),
            item("a").copy(valueSource = "initial-guide"), item("a").copy(count = 2, maxStack = 2))) {
            assertEquals(BackpackReplacementPlanner.Result.NoUpgrade,
                plan(snapshot(carried = listOf(a) + cheap.drop(1)), protected = allProtected))
        }
        assertEquals(listOf("b"), ready(plan(snapshot(), protected = setOf("a"))).drops.map { it.gid })
    }
    @Test fun rigAndSecureCaseCannotBeFreed() {
        for (id in listOf("rig", "secure")) {
            val s = snapshot(grids = listOf(Grid(id, 2, 2, setOf(0, 1, 2, 3))))
            val otherPlaces = places.mapValues { (_, p) -> p.copy(grid = id) }
            assertEquals(BackpackReplacementPlanner.Result.NoUpgrade, plan(s, otherPlaces, emptySet()))
        }
    }
    @Test fun fragmentedSpaceRequiresTheActualBlockingItem() {
        val carried = listOf(item("left", 5_000, 1, 2), item("right", 2_000, 1, 2))
        val loc = mapOf("left" to Placement("bag", 0, 0, false), "right" to Placement("bag", 1, 0, false))
        val p = ready(plan(snapshot(incoming = listOf(item("new", 8_000, 1, 2)), carried = carried), loc))
        assertEquals(listOf("right"), p.drops.map { it.gid })
    }
    @Test fun largeBlockingItemIsChargedOnceForItsWholeValue() {
        val large = item("large", 6_000, 2, 2)
        val p = ready(plan(snapshot(carried = listOf(large)), mapOf("large" to Placement("bag", 0, 0, false))))
        assertEquals(6_000L, p.lostValue); assertEquals(listOf("large"), p.drops.map { it.gid })
    }
    @Test fun staleAndIncompleteOccupancyNeverProducesDropPlan() {
        for (s in listOf(snapshot().copy(capturedAtMs = 500), snapshot().copy(storageComplete = false)))
            assertEquals(BackpackReplacementPlanner.Result.InvalidContext, plan(s))
        assertEquals(BackpackReplacementPlanner.Result.InvalidContext, plan(snapshot(), places - "a"))
        val overlapping = places + ("b" to places.getValue("a"))
        assertEquals(BackpackReplacementPlanner.Result.InvalidContext, plan(snapshot(), overlapping))
    }
    @Test fun mostExpensiveIncomingHasPriorityEvenWhenSmallerCheaperItemFits() {
        val s = snapshot(incoming = listOf(item("small", 2_000), item("large", 10_000, 2, 2)),
            carried = cheap.take(3), grids = listOf(Grid("bag", 2, 2, setOf(0, 1, 2))))
        val p = ready(plan(s, places - "d"))
        assertEquals("large", p.incoming.gid); assertEquals(6_000L, p.lostValue)
    }
}
