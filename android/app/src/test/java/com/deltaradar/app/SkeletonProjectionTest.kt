package com.deltaradar.app

import org.junit.Assert.*
import org.junit.Test

class SkeletonProjectionTest {
    private val camera = SkeletonProjection.Joint(0.0, 0.0, 0.0)
    private fun project(joints: List<SkeletonProjection.Joint>, parents: IntArray = intArrayOf(-1, 0)) =
        SkeletonProjection.project(joints, parents, camera, 0.0, 0.0, 90.0, 2.0)

    @Test fun depthChangesBoneProjectionAndPreservesTheRealHierarchy() {
        val pose = project(listOf(SkeletonProjection.Joint(10.0, 2.0, 1.0),
            SkeletonProjection.Joint(20.0, 2.0, 1.0)))!!
        assertEquals(0.6f, pose.x[0], 0.0001f)
        assertEquals(0.55f, pose.x[1], 0.0001f)
        assertEquals(0.4f, pose.y[0], 0.0001f)
        assertEquals(0.45f, pose.y[1], 0.0001f)
        assertArrayEquals(intArrayOf(-1, 0), pose.parents)
    }

    @Test fun offscreenJointIsNotMovedToTheTracerEdge() {
        val pose = project(listOf(SkeletonProjection.Joint(10.0, 0.0, 0.0),
            SkeletonProjection.Joint(10.0, 20.0, 0.0)))!!
        assertEquals(0.5f, pose.x[0], 0.0001f)
        assertEquals(1.5f, pose.x[1], 0.0001f)
    }

    @Test fun rearJointsCannotCreateMirroredBones() {
        val pose = project(listOf(SkeletonProjection.Joint(10.0, 0.0, 0.0),
            SkeletonProjection.Joint(-10.0, 0.0, 0.0)))!!
        assertTrue(pose.x[0].isFinite())
        assertTrue(pose.x[1].isNaN())
        assertTrue(pose.y[1].isNaN())
    }

    @Test fun cameraRollIsAppliedToTheSameWorldJoints() {
        val pose=SkeletonProjection.project(listOf(SkeletonProjection.Joint(10.0,2.0,0.0),
            SkeletonProjection.Joint(10.0,2.0,1.0)),intArrayOf(-1,0),camera,0.0,0.0,90.0,2.0,90.0)!!
        assertEquals(.5f,pose.x[0],.0001f)
        assertEquals(.3f,pose.y[0],.0001f)
        assertEquals(.45f,pose.x[1],.0001f)
        assertEquals(.3f,pose.y[1],.0001f)
    }

    @Test fun malformedCoordinatesAndCyclesRejectThePose() {
        val points = listOf(SkeletonProjection.Joint(10.0, 0.0, 0.0), SkeletonProjection.Joint(11.0, 1.0, 0.0))
        assertNull(project(points, intArrayOf(-1, 1)))
        assertNull(project(points, intArrayOf(-1, 2)))
        assertNull(project(points, intArrayOf(0, 0)))
        assertNull(project(points, intArrayOf(-1)))
        assertNull(project(listOf(points[0], SkeletonProjection.Joint(Double.NaN, 0.0, 0.0))))
    }
}
