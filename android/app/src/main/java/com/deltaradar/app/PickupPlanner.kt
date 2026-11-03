package com.deltaradar.app

/** Decision logic only. The current radar has no adapter that establishes this
 * contract from live inventory/UI data and no input/transfer implementation.
 * Never construct an actionable snapshot from world markers or Nearby alone. */
object PickupPlanner {
    const val MAX_AGE_MS = 300L

    data class Item(
        val gid: String, val catalogId: Long, val count: Int,
        val width: Int, val height: Int, val unitValue: Long?,
        val valueSource: String = "", val maxStack: Int = 1,
        val searched: Boolean = false
    )

    /** Occupied cells must include all carried objects, including unpriced gear.
     * Only verified backpack/rig grids belong here. Do not include the stash,
     * equipped slots, pockets, secure case, or space inferred from item totals. */
    data class Grid(val id: String, val width: Int, val height: Int, val occupied: Set<Int>)
    data class Snapshot(
        val session: String, val container: String, val window: String, val sequence: Long,
        val capturedAtMs: Long, val windowOpen: Boolean,
        val sourceComplete: Boolean, val storageComplete: Boolean,
        val items: List<Item>, val carried: List<Item>, val grids: List<Grid>
    )
    data class Placement(val grid: String, val x: Int, val y: Int, val rotated: Boolean)
    data class Candidate(val item: Item, val placement: Placement)

    enum class StopReason { INVALID_CONTEXT, NO_ELIGIBLE_ITEM, CONTEXT_CHANGED, TRANSFER_UNCONFIRMED, CANCELLED }
    sealed class Decision {
        data class Take(val candidate: Candidate) : Decision()
        data object Wait : Decision()
        data class Stop(val reason: StopReason) : Decision()
    }

    private fun Item.valid(): Boolean = gid.isNotBlank() && catalogId > 0 && count in 1..100_000 &&
        width in 1..16 && height in 1..16 && maxStack in 1..100_000 && count <= maxStack

    private fun Snapshot.valid(nowMs: Long): Boolean {
        if (session.isBlank() || container.isBlank() || window.isBlank() || sequence < 0 || capturedAtMs < 0 ||
            nowMs < capturedAtMs || nowMs - capturedAtMs >= MAX_AGE_MS || !windowOpen ||
            !sourceComplete || !storageComplete || items.size > 256 || carried.size > 256 || grids.size > 64)
            return false
        val all = items + carried
        if (all.any { !it.valid() } || all.map { it.gid }.toSet().size != all.size ||
            grids.map { it.id }.toSet().size != grids.size) return false
        return grids.all { g -> g.id.isNotBlank() && g.width in 1..16 && g.height in 1..16 &&
            g.occupied.all { it >= 0 && it < g.width * g.height } }
    }

    private fun Item.eligible(minimum: Int): Boolean {
        val category = catalogId / 10_000
        val subType = (category / 1_000) % 100
        return searched && category / 100_000 == 15L && subType in 1L..9L && subType != 5L &&
            // Initial transfer confirmation tracks a single nonstacking object.
            count == 1 && maxStack == 1 && unitValue != null &&
            LootValuePolicy.passes(true, minimum, true, valueSource, unitValue.toDouble())
    }

    private fun placement(item: Item, grids: List<Grid>): Placement? {
        for (grid in grids) {
            for (rotated in listOf(false, true)) {
                if (rotated && item.width == item.height) continue
                val w = if (rotated) item.height else item.width
                val h = if (rotated) item.width else item.height
                if (w > grid.width || h > grid.height) continue
                for (y in 0..grid.height - h) for (x in 0..grid.width - w) {
                    if ((y until y + h).all { row ->
                            (x until x + w).all { col -> row * grid.width + col !in grid.occupied }
                        }) return Placement(grid.id, x, y, rotated)
                }
            }
        }
        return null
    }

    /** Absolute individual value first, then smaller footprint, then stable ID.
     * Select one object that fits; recalculate from fresh data after transfer.
     * This is a priority policy, not a global knapsack or value-per-cell solver. */
    fun next(snapshot: Snapshot, nowMs: Long, minimum: Int): Decision {
        if (minimum < 0 || !snapshot.valid(nowMs)) return Decision.Stop(StopReason.INVALID_CONTEXT)
        val ordered = snapshot.items.filter { it.eligible(minimum) }
            .sortedWith(compareByDescending<Item> { it.unitValue }
                .thenBy { it.width * it.height }.thenBy { it.gid })
        for (item in ordered) {
            val position = placement(item, snapshot.grids) ?: continue
            return Decision.Take(Candidate(item, position))
        }
        return Decision.Stop(StopReason.NO_ELIGIBLE_ITEM)
    }

    /** One button press, one open container. No retries, drops, swaps, or second
     * command while a transfer is pending. Cancelled/stopped sessions cannot
     * restart until the caller creates a new session for another button press. */
    class Session(private val minimum: Int, private val confirmationTimeoutMs: Long = 1_500L) {
        init { require(minimum >= 0 && confirmationTimeoutMs in 1..10_000) }
        private var context: Triple<String, String, String>? = null
        private var lastSequence = -1L
        private var lastCapture = -1L
        private var issuedSequence = -1L
        private var issuedCapture = -1L
        private var pending: Candidate? = null
        private var issuedAt = 0L
        private var stopped: StopReason? = null

        fun cancel() { stopped = StopReason.CANCELLED; pending = null }
        private fun stop(reason: StopReason): Decision.Stop {
            stopped = reason; pending = null; return Decision.Stop(reason)
        }

        fun advance(snapshot: Snapshot, nowMs: Long): Decision {
            stopped?.let { return Decision.Stop(it) }
            if (!snapshot.valid(nowMs)) return stop(StopReason.INVALID_CONTEXT)
            val identity = Triple(snapshot.session, snapshot.container, snapshot.window)
            if (context != null && context != identity) return stop(StopReason.CONTEXT_CHANGED)
            if (nowMs < issuedAt || snapshot.sequence < lastSequence || snapshot.capturedAtMs < lastCapture)
                return stop(StopReason.INVALID_CONTEXT)
            context = identity
            val repeated = snapshot.sequence == lastSequence
            if (repeated && snapshot.capturedAtMs != lastCapture) return stop(StopReason.INVALID_CONTEXT)
            lastSequence = snapshot.sequence; lastCapture = snapshot.capturedAtMs
            val previous = pending
            if (previous != null) {
                if (nowMs - issuedAt >= confirmationTimeoutMs) return stop(StopReason.TRANSFER_UNCONFIRMED)
                val source = snapshot.items.firstOrNull { it.gid == previous.item.gid }
                val target = snapshot.carried.firstOrNull { it.gid == previous.item.gid }
                val confirmed = snapshot.sequence > issuedSequence && snapshot.capturedAtMs > issuedCapture &&
                    snapshot.capturedAtMs >= issuedAt &&
                    source == null && target?.catalogId == previous.item.catalogId && target.count == 1 &&
                    target.width == previous.item.width && target.height == previous.item.height
                // Missing from the source is insufficient: death, refresh,
                // another player's pickup, or stale cache could also remove it.
                if (!confirmed) {
                    return Decision.Wait
                }
                pending = null
            } else if (repeated) return Decision.Wait
            return when (val decision = next(snapshot, nowMs, minimum)) {
                is Decision.Take -> {
                    pending = decision.candidate; issuedAt = nowMs
                    issuedSequence = snapshot.sequence; issuedCapture = snapshot.capturedAtMs
                    decision
                }
                is Decision.Stop -> stop(decision.reason)
                Decision.Wait -> Decision.Wait
            }
        }
    }
}
