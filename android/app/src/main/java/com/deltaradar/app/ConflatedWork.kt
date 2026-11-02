package com.deltaradar.app

/** One in-flight operation and one request to use the latest state afterwards. */
class ConflatedWork {
    private var active = false
    private var requested = false
    private var closed = false

    @Synchronized fun request(): Boolean {
        if (closed) return false
        if (active) { requested = true; return false }
        active = true
        return true
    }

    /** Keep ownership across worker computation and main-thread publication. */
    @Synchronized fun finish(): Boolean {
        if (closed) return false
        if (requested) { requested = false; return true }
        active = false
        return false
    }

    @Synchronized fun close() { closed = true; requested = false }
}
