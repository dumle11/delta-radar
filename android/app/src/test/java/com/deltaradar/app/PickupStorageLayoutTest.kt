package com.deltaradar.app

import com.deltaradar.app.PickupStorageLayout.Kind
import com.deltaradar.app.PickupStorageLayout.Shape
import com.deltaradar.app.PickupStorageLayout.Space
import com.deltaradar.app.PickupStorageLayout.Position
import com.deltaradar.app.PickupStorageLayout.Slot
import org.junit.Assert.*
import org.junit.Test

class PickupStorageLayoutTest {
    private fun space(i: Int = 1, w: Int = 3, h: Int = 5) = Space(i, w, h, false, false)
    private fun position(gid: String = "synthetic-reader", sub: Int = 1, x: Int = 0, y: Int = 0,
                         w: Int = 1, h: Int = 1, rotated: Boolean = false) =
        Position(gid, sub, x, y, x, x + w - 1, y, y + h - 1, w, h, rotated)
    private fun bag(items: List<Shape> = listOf(Shape("synthetic-reader", 1, 1)),
                    positions: List<Position> = listOf(position())) =
        Slot("bag", Kind.BACKPACK, 15, true, items, listOf(space()), positions)

    @Test fun normalizesBackpackAndKeepsUnknownPriceObjectsInOccupancy() {
        val decoded = PickupStorageLayout.decode(listOf(bag()))!!
        assertEquals(3, decoded.grids.single().width)
        assertEquals(5, decoded.grids.single().height)
        assertEquals(setOf(0), decoded.grids.single().occupied)
        assertEquals(setOf(decoded.grids.single().id), decoded.backpackGridIds)
    }

    @Test fun rawLengthIsHorizontalAndRotationChangesThePlacedFootprint() {
        val shape = Shape("synthetic-pipe", 1, 2)
        for (turn in listOf(false, true)) {
            val p = position(shape.gid, w = if (turn) 1 else 2, h = if (turn) 2 else 1, rotated = turn)
            val decoded = PickupStorageLayout.decode(listOf(bag(listOf(shape), listOf(p))))!!
            assertEquals(if (turn) setOf(0, 3) else setOf(0, 1), decoded.grids.single().occupied)
            assertEquals(turn, decoded.locations.getValue(shape.gid).rotated)
        }
    }

    @Test fun rigCompartmentsDoNotBecomeOneLargeRectangle() {
        val rig = Slot("rig", Kind.RIG, 8, true, emptyList(),
            listOf(space(1, 1, 2), space(2, 1, 2), space(3, 1, 2), space(4, 1, 1), space(5, 1, 1)), emptyList())
        val decoded = PickupStorageLayout.decode(listOf(bag(), rig))!!
        assertEquals(6, decoded.grids.size)
        assertEquals(1, decoded.backpackGridIds.size)
        assertTrue(decoded.grids.drop(1).all { it.width == 1 })
    }

    @Test fun capacityIsRefreshedAndUnequippedEmptySlotHasNoFreeCells() {
        val empty = Slot("rig", Kind.RIG, 0, true, emptyList(), emptyList(), emptyList())
        val larger = bag().copy(capacity = 18, spaces = listOf(space(h = 6)))
        assertEquals(18, PickupStorageLayout.decode(listOf(larger, empty))!!.grids.single().let { it.width * it.height })
        assertNull(PickupStorageLayout.decode(listOf(larger.copy(capacity = 15))))
    }

    @Test fun missingExtraDuplicateAndOverlappingObjectsRejectTheCopy() {
        val b = bag()
        for (broken in listOf(b.copy(positions = emptyList()), b.copy(items = emptyList()),
            b.copy(positions = b.positions + b.positions), b.copy(items = b.items + b.items),
            b.copy(items = b.items + Shape("synthetic-other", 1, 1),
                positions = b.positions + position("synthetic-other"))))
            assertNull(PickupStorageLayout.decode(listOf(broken)))
    }

    @Test fun rejectsWrongBoundsFootprintCompartmentAndLockedOrIncompleteSpace() {
        val b = bag()
        for (p in listOf(position(x = -1), position(x = 3), position(y = 5), position(sub = 2),
            position().copy(right = 1), position().copy(bottom = 1), position(w = 2)))
            assertNull(PickupStorageLayout.decode(listOf(b.copy(positions = listOf(p)))))
        for (s in listOf(space().copy(fullyLocked = true), space().copy(fullyLocked = null),
            space().copy(multiSpace = true), space().copy(multiSpace = null)))
            assertNull(PickupStorageLayout.decode(listOf(b.copy(spaces = listOf(s)))))
        assertNull(PickupStorageLayout.decode(listOf(b.copy(complete = false))))
    }

    @Test fun rejectsDuplicateSlotsCompartmentsAndCrossSlotItemIdentity() {
        val b = bag()
        assertNull(PickupStorageLayout.decode(listOf(b, b)))
        assertNull(PickupStorageLayout.decode(listOf(b.copy(spaces = listOf(space(), space()), capacity = 30))))
        val rig = b.copy(id = "rig", kind = Kind.RIG)
        assertNull(PickupStorageLayout.decode(listOf(b, rig)))
        assertNull(PickupStorageLayout.decode(listOf(b.copy(items = listOf(Shape("synthetic-reader", 0, 1))))))
    }
}
