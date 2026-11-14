package com.dme.launcher.overlay

import org.json.JSONObject

internal data class LiveCamera(
    val gamePid: Int, val world: String, val sampledAt: Long, val timestamp: Double,
    val valid: Boolean, val x: Double, val y: Double, val z: Double,
    val yaw: Double, val pitch: Double, val roll: Double, val fov: Double
) {
    fun usable(pid: Int, currentWorld: String, now: Long): Boolean =
        valid && gamePid == pid && world == currentWorld && sampledAt > 0 &&
            now-sampledAt in 0..100 && timestamp.isFinite() && timestamp > 0 &&
            listOf(x,y,z,yaw,pitch,roll,fov).all { it.isFinite() } && fov in 5.0..150.0
    fun json() = JSONObject().put("x",x).put("y",y).put("z",z).put("yaw",yaw)
        .put("pitch",pitch).put("roll",roll).put("fov",fov)
    companion object {
        fun parse(j: JSONObject) = LiveCamera(j.optInt("gp"),j.optString("w"),
            j.optLong("sa"),j.optDouble("ts",-1.0),j.optBoolean("v"),
            j.optDouble("x",Double.NaN),j.optDouble("y",Double.NaN),j.optDouble("z",Double.NaN),
            j.optDouble("yw",Double.NaN),j.optDouble("pi",Double.NaN),j.optDouble("r",Double.NaN),
            j.optDouble("fv",Double.NaN))
    }
}
