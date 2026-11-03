package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class ProjectionSchedulingTest {
    @Test fun burstWhileWorkerAndUiAreBusyKeepsOnlyOneFollowup() {
        val work = ConflatedWork()
        assertTrue(work.request())
        repeat(1_000) { assertFalse(work.request()) }
        // Do not release until the UI has published, even after computation ends.
        repeat(1_000) { assertFalse(work.request()) }
        assertTrue(work.finish())
        assertFalse(work.finish())
        assertTrue(work.request())
        assertFalse(work.finish())
    }

    @Test fun newFrameDuringFollowupIsNotLostAndDestroyCannotReschedule() {
        val work = ConflatedWork()
        assertTrue(work.request())
        assertFalse(work.request())
        assertTrue(work.finish())
        assertFalse(work.request())
        assertTrue(work.finish())
        work.close()
        assertFalse(work.finish())
        assertFalse(work.request())
    }

    @Test fun feedRestartDoesNotWaitForItsSequenceToCatchUp() {
        val gate = FramePublicationGate()
        val oldFeed = Any()
        val newFeed = Any()
        assertTrue(gate.accept(oldFeed, 1, 900))
        assertFalse(gate.accept(oldFeed, 1, 899))
        assertTrue(gate.accept(newFeed, 1, 1))
        assertFalse(gate.accept(newFeed, 1, 1))
        assertTrue(gate.accept(newFeed, 1, 2))
    }

    @Test fun showAndStyleChangesCanRepublishTheSameFreshFrame() {
        val gate = FramePublicationGate()
        val feed = Any()
        assertTrue(gate.accept(feed, 1, 10))
        assertTrue(gate.alreadyPublished(feed, 1, 10))
        assertTrue(gate.accept(feed, 2, 10))
        gate.reset()
        assertFalse(gate.alreadyPublished(feed, 2, 10))
        assertTrue(gate.accept(feed, 2, 10))
    }
}
