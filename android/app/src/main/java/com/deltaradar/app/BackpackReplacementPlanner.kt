package com.deltaradar.app

import com.deltaradar.app.PickupPlanner.Item
import com.deltaradar.app.PickupPlanner.Placement
import com.deltaradar.app.PickupPlanner.Snapshot

/** Plans only: no drop, tap, transfer or inventory mutation. All removals are
 * decided before the first action. Only verified backpack grids may be freed. */
object BackpackReplacementPlanner {
    data class Plan(
        val incoming: Item, val placement: Placement, val drops: List<Item>,
        val lostValue: Long, val valueGain: Long
    )
    sealed class Result {
        data class Ready(val plan: Plan) : Result()
        data object NoUpgrade : Result()
        data object InvalidContext : Result()
    }

    /** locations includes every object occupying the supplied storage grids,
     * including unknown-price items. Protected/unknown/stacked/unsupported
     * objects block space but can never become drop candidates here. */
    fun plan(
        snapshot: Snapshot, locations: Map<String, Placement>, backpackGridIds: Set<String>,
        nowMs: Long, minimum: Int, protectedGids: Set<String> = emptySet()
    ): Result {
        if (minimum < 0 || !PickupPlanner.validSnapshot(snapshot, nowMs)) return Result.InvalidContext
        val grids = snapshot.grids.associateBy { it.id }
        val carried = snapshot.carried.associateBy { it.gid }
        if (!grids.keys.containsAll(backpackGridIds)) return Result.InvalidContext
        val owners = grids.mapValues { (_, g) -> arrayOfNulls<String>(g.width * g.height) }
        for ((gid, place) in locations) {
            val item = carried[gid] ?: return Result.InvalidContext
            val grid = grids[place.grid] ?: return Result.InvalidContext
            val cells = owners.getValue(place.grid)
            val w = if (place.rotated) item.height else item.width
            val h = if (place.rotated) item.width else item.height
            if (place.x < 0 || place.y < 0 || w > grid.width || h > grid.height ||
                place.x > grid.width - w || place.y > grid.height - h) return Result.InvalidContext
            for (y in place.y until place.y + h) for (x in place.x until place.x + w) {
                val index = y * grid.width + x
                if (cells[index] != null) return Result.InvalidContext
                cells[index] = gid
            }
        }
        for ((id, grid) in grids) {
            val occupied = owners.getValue(id).indices.filter { owners.getValue(id)[it] != null }.toSet()
            if (occupied != grid.occupied) return Result.InvalidContext
        }
        val ordered = snapshot.items.filter { PickupPlanner.eligibleItem(it, minimum) }
            .sortedWith(compareByDescending<Item> { it.unitValue }
                .thenBy { it.width * it.height }.thenBy { it.gid })
        for (incoming in ordered) {
            // Preserve individual-value priority even when a cheaper, smaller
            // object would fit immediately. The game chooses the destination;
            // this placement is a fit check, not an instruction to rearrange.
            val free = PickupPlanner.next(snapshot.copy(items = listOf(incoming)), nowMs, minimum)
            if (free is PickupPlanner.Decision.Take) {
                return Result.Ready(Plan(incoming, free.candidate.placement, emptyList(), 0, incoming.unitValue!!))
            }
            var best: Plan? = null
            for (grid in snapshot.grids.filter { it.id in backpackGridIds }) {
                val cells = owners.getValue(grid.id)
                for (turn in 0..1) {
                    val rotated = turn == 1
                    if (rotated && incoming.width == incoming.height) continue
                    val w = if (rotated) incoming.height else incoming.width
                    val h = if (rotated) incoming.width else incoming.height
                    if (w > grid.width || h > grid.height) continue
                    for (y in 0..grid.height - h) for (x in 0..grid.width - w) {
                        val blocked = linkedSetOf<String>()
                        for (row in y until y + h) for (col in x until x + w)
                            cells[row * grid.width + col]?.let { blocked.add(it) }
                        if (blocked.any { it in protectedGids }) continue
                        val drops = blocked.map { carried.getValue(it) }
                        if (drops.any { !PickupPlanner.eligibleItem(it, 0) }) continue
                        val loss = drops.sumOf { it.unitValue!! }
                        val gain = incoming.unitValue!! - loss
                        if (gain <= 0) continue
                        val previous = best
                        if (previous == null || loss < previous.lostValue ||
                            (loss == previous.lostValue && drops.size < previous.drops.size)) {
                            best = Plan(incoming, Placement(grid.id, x, y, rotated),
                                drops.sortedWith(compareBy<Item> { it.unitValue }.thenBy { it.gid }), loss, gain)
                        }
                    }
                }
            }
            // Incoming items have individual-value priority. Within that item,
            // sacrifice the cheapest complete blocking set that makes it fit.
            if (best != null) return Result.Ready(best)
        }
        return Result.NoUpgrade
    }
}
