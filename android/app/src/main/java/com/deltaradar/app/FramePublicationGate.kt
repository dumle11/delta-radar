package com.deltaradar.app

/** Sequence numbers belong to a feed instance and a visible overlay session. */
class FramePublicationGate {
    private var source: Any? = null
    private var visibilityGeneration = -1L
    private var sequence = -1L

    @Synchronized fun alreadyPublished(candidate: Any, generation: Long, next: Long): Boolean =
        source === candidate && visibilityGeneration == generation && next <= sequence

    @Synchronized fun accept(candidate: Any, generation: Long, next: Long): Boolean {
        if (alreadyPublished(candidate, generation, next)) return false
        source = candidate; visibilityGeneration = generation; sequence = next
        return true
    }

    @Synchronized fun reset() { source = null; sequence = -1 }
}
