package com.deltaradar.app

/** Converts copied inventory layout fields to planner grids. This establishes
 * storage occupancy only, not an open loot window or actionable UI coordinates.
 * The adapter remains responsible for fresh, complete, immutable table copies. */
object PickupStorageLayout {
    enum class Kind { BACKPACK, RIG }
    data class Shape(val gid: String, val rawWidth: Int, val rawLength: Int)
    data class Space(
        val index: Int, val length: Int, val height: Int,
        val fullyLocked: Boolean?, val multiSpace: Boolean?
    )
    data class Position(
        val gid: String, val subIndex: Int, val x: Int, val y: Int,
        val left: Int, val right: Int, val top: Int, val bottom: Int,
        val length: Int, val height: Int, val rotated: Boolean
    )
    data class Slot(
        val id: String, val kind: Kind, val capacity: Int, val complete: Boolean,
        val items: List<Shape>, val spaces: List<Space>, val positions: List<Position>
    )
    data class Storage(
        val grids: List<PickupPlanner.Grid>,
        val locations: Map<String, PickupPlanner.Placement>,
        val backpackGridIds: Set<String>
    )

    /** Raw item length is horizontal columns; raw width is vertical rows.
     * Each space batch is a separate rectangle, especially in a rig. Inclusive
     * position bounds and rotated footprints must agree with the item shape.
     * Any missing/duplicate/overlapping object rejects the whole storage copy. */
    fun decode(slots: List<Slot>): Storage? {
        if (slots.isEmpty() || slots.size > 2 || slots.map { it.id }.toSet().size != slots.size ||
            slots.map { it.kind }.toSet().size != slots.size ||
            slots.sumOf { it.items.size.toLong() } > 256) return null
        val grids = mutableListOf<PickupPlanner.Grid>()
        val locations = linkedMapOf<String, PickupPlanner.Placement>()
        val bags = linkedSetOf<String>()
        for (slot in slots) {
            if (!slot.complete || slot.id.isBlank() || slot.id.length > 96 || slot.capacity !in 0..256 ||
                slot.items.size > 256 || slot.positions.size > 256 || slot.spaces.size > 64 ||
                slot.items.any { it.gid.isBlank() || it.rawWidth !in 1..16 || it.rawLength !in 1..16 } ||
                slot.items.map { it.gid }.toSet().size != slot.items.size ||
                slot.positions.map { it.gid }.toSet().size != slot.positions.size ||
                slot.items.map { it.gid }.toSet() != slot.positions.map { it.gid }.toSet()) return null
            if (slot.spaces.any { it.index !in 1..64 || it.length !in 1..16 || it.height !in 1..16 ||
                    it.fullyLocked != false || it.multiSpace != false } ||
                slot.spaces.map { it.index }.toSet().size != slot.spaces.size ||
                slot.spaces.sumOf { it.length * it.height } != slot.capacity ||
                grids.size + slot.spaces.size > 64) return null
            val spaces = slot.spaces.associateBy { it.index }
            val occupied = spaces.mapValues { linkedSetOf<Int>() }
            val shapes = slot.items.associateBy { it.gid }
            for (p in slot.positions) {
                val shape = shapes[p.gid] ?: return null
                val space = spaces[p.subIndex] ?: return null
                val columns = if (p.rotated) shape.rawWidth else shape.rawLength
                val rows = if (p.rotated) shape.rawLength else shape.rawWidth
                if (p.x < 0 || p.y < 0 || p.length != columns || p.height != rows ||
                    columns > space.length || rows > space.height ||
                    p.x > space.length - columns || p.y > space.height - rows ||
                    p.left != p.x || p.top != p.y || p.right != p.x + columns - 1 ||
                    p.bottom != p.y + rows - 1 || p.gid in locations) return null
                val cells = occupied.getValue(p.subIndex)
                for (row in p.y until p.y + rows) for (col in p.x until p.x + columns)
                    if (!cells.add(row * space.length + col)) return null
                locations[p.gid] = PickupPlanner.Placement(gridId(slot, space), p.x, p.y, p.rotated)
            }
            for (space in slot.spaces) {
                val id = gridId(slot, space)
                grids.add(PickupPlanner.Grid(id, space.length, space.height,
                    occupied.getValue(space.index).toSet()))
                if (slot.kind == Kind.BACKPACK) bags.add(id)
            }
        }
        return Storage(grids.toList(), locations.toMap(), bags.toSet())
    }

    private fun gridId(slot: Slot, space: Space) = "${slot.kind.name}:${slot.id}:${space.index}"
}
