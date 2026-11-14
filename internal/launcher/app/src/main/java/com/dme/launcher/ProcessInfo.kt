package com.dme.launcher

data class ProcessInfo(val pid: Int, val name: String) {
    override fun toString(): String = "$name (PID $pid)"
}

object ProcessLister {

    fun listGameProcesses(): List<ProcessInfo> {
        val output = RootShell.exec("ps -eo PID,NAME 2>/dev/null || ps -A -o PID,NAME 2>/dev/null || ps -A")
        if (output.isBlank()) return emptyList()

        return output.lines()
            .drop(1) // skip header
            .mapNotNull { line ->
                val parts = line.trim().split(Regex("\\s+"))
                if (parts.size >= 2) {
                    val pid = parts[0].toIntOrNull()
                    val name = parts.last().trim()
                    if (pid != null && pid > 100 && !isSystemProcess(name)) {
                        ProcessInfo(pid, name)
                    } else null
                } else null
            }
            .distinctBy { it.name }
            .sortedBy { it.name.lowercase() }
    }

    private fun isSystemProcess(name: String): Boolean {
        val skip = listOf(
            "/system/", "/vendor/", "/apex/", "zygote", "servicemanager",
            "surfaceflinger", "installd", "vold", "logd", "healthd",
            "lmkd", "storaged", "thermalserviced", "traced", "adbd",
            "sh", "toybox", "toolbox", "magisk", "ksu"
        )
        val lower = name.lowercase()
        return skip.any { lower.contains(it) }
    }
}
