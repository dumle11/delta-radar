package com.dme.launcher.overlay

import kotlin.math.sqrt

/** Short, bounded world-space compensation; camera transforms are never blended. */
internal object WorldMotion {
    data class Velocity(val x: Double = 0.0, val y: Double = 0.0, val z: Double = 0.0)
    fun velocity(x: Double, y: Double, z: Double, at: Long,
                 oldX: Double, oldY: Double, oldZ: Double, oldAt: Long): Velocity {
        val elapsed = at - oldAt
        if (oldAt <= 0 || elapsed !in 1..250) return Velocity()
        val dt = elapsed / 1000.0
        val v = Velocity((x-oldX)/dt, (y-oldY)/dt, (z-oldZ)/dt)
        val speed = sqrt(v.x*v.x+v.y*v.y+v.z*v.z)
        return if (speed.isFinite() && speed <= 15.0) v else Velocity()
    }
    fun offset(velocity: Velocity, sampledAt: Long, now: Long): Velocity {
        if (sampledAt <= 0 || now < sampledAt || now-sampledAt > 150) return Velocity()
        val seconds = (now-sampledAt).coerceAtMost(75) / 1000.0
        return Velocity(velocity.x*seconds, velocity.y*seconds, velocity.z*seconds)
    }
}
