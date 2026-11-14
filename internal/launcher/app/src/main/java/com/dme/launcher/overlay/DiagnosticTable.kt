package com.dme.launcher.overlay

internal data class DiagnosticActor(
    val address: String,
    val kind: String,
    val name: String,
    val classAddress: String,
    val className: String,
    val health: String,
    val status: String,
    val position: String
)

internal data class DiagnosticReport(
    val receivedAt: Long,
    val status: String,
    val gworld: String,
    val world: String,
    val level: String,
    val actorArray: String,
    val actorCount: String,
    val readFailures: Int,
    val failureAddress: String,
    val failureErrno: Int,
    val actors: List<DiagnosticActor>,
    val totalEntities: Int
)

internal object DiagnosticTable {
    const val MAX_ROWS = 64

    private fun cell(value: String, width: Int): String =
        value.replace('\n', ' ').replace('\r', ' ').take(width).padEnd(width)

    fun format(report: DiagnosticReport?, now: Long): String {
        if (report == null) return "Diagnostics unavailable: no data snapshot."
        val age = (now - report.receivedAt).coerceAtLeast(0)
        val freshness = if (age > 3_000) "STALE" else "current"
        val out = StringBuilder()
        out.append("Snapshot: $freshness (${age / 1000}s old) | ${report.status}\n")
        out.append("GWorld slot | ${report.gworld}\n")
        out.append("UWorld      | ${report.world}\n")
        out.append("Level       | ${report.level}\n")
        out.append("Actor array | ${report.actorArray} | count ${report.actorCount}\n")
        out.append("Read failures in latest sample: ${report.readFailures}")
        if (report.readFailures > 0) {
            val reason = if (report.failureErrno == 0) "short read" else "errno ${report.failureErrno}"
            out.append(" | last ${report.failureAddress} ($reason)")
        }
        val displayed = report.actors.take(MAX_ROWS)
        if (report.totalEntities < 0) out.append("\nEntity rows not included in this sample.\n")
        else out.append("\nFiltered published entities: ${report.totalEntities}; showing ${displayed.size}\n")
        out.append("Address            | Kind   | Name                 | Class address      | Class                | Health      | Status | Position\n")
        for (actor in displayed) {
            out.append(cell(actor.address, 18)).append(" | ")
                .append(cell(actor.kind, 6)).append(" | ")
                .append(cell(actor.name, 20)).append(" | ")
                .append(cell(actor.classAddress, 18)).append(" | ")
                .append(cell(actor.className, 20)).append(" | ")
                .append(cell(actor.health, 11)).append(" | ")
                .append(cell(actor.status, 6)).append(" | ")
                .append(actor.position).append('\n')
        }
        out.append("alive* = no death flag detected, not proof of health. ")
        out.append("Player markers without current position/camera checks are hidden. ")
        out.append("Checks passing do not guarantee a correct location; unverified does not establish encryption.")
        return out.toString()
    }
}
