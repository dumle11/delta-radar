package com.deltaradar.app

/** Projects the mesh's animated world-space joints and actual parent hierarchy. */
internal object SkeletonProjection {
    const val MAX_BONES = 512
    data class Joint(val x: Double, val y: Double, val z: Double) {
        fun isFinite() = x.isFinite() && y.isFinite() && z.isFinite()
    }
    data class Pose(val x: FloatArray, val y: FloatArray, val parents: IntArray)

    fun project(
        joints: List<Joint>, parents: IntArray, camera: Joint,
        yaw: Double, pitch: Double, fov: Double, aspect: Double
    ): Pose? {
        val count = joints.size
        if (count !in 2..MAX_BONES || parents.size != count || !camera.isFinite() ||
            parents[0] != -1 || !yaw.isFinite() || !pitch.isFinite() || !fov.isFinite() ||
            !aspect.isFinite() || aspect <= 0 || joints.any { !it.isFinite() }) return null
        for (i in 1 until count) if (parents[i] !in 0 until i) return null
        val x = FloatArray(count)
        val y = FloatArray(count)
        for ((i, joint) in joints.withIndex()) {
            val point = ProjectionMath.project(
                joint.x - camera.x, joint.y - camera.y, joint.z - camera.z,
                yaw, pitch, fov, aspect, clipToEdge = false
            )
            // Offscreen joints keep their true perspective coordinates. Canvas
            // clips segments at the viewport; rear joints cannot form a segment.
            x[i] = if (point.inFront) point.x else Float.NaN
            y[i] = if (point.inFront) point.y else Float.NaN
        }
        return Pose(x, y, parents.copyOf())
    }
}
