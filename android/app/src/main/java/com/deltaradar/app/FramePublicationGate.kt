package com.deltaradar.app

/** Sequence numbers belong to a feed instance and a visible overlay session. */
class FramePublicationGate {
    private var source: Any? = null
    private var visibilityGeneration = -1L
    private var sequence = -1L
    private var cameraSequence = -1L

    @Synchronized fun alreadyPublished(candidate: Any, generation: Long, next: Long, camera: Long = 0): Boolean =
        source === candidate && visibilityGeneration == generation && (next < sequence || (next == sequence && camera <= cameraSequence))

    @Synchronized fun accept(candidate: Any, generation: Long, next: Long, camera: Long = 0): Boolean {
        if (alreadyPublished(candidate, generation, next, camera)) return false
        source = candidate; visibilityGeneration = generation; sequence = next; cameraSequence = camera
        return true
    }

    @Synchronized fun reset() { source = null; sequence = -1; cameraSequence = -1 }
}
