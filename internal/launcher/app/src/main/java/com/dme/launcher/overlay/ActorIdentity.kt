package com.dme.launcher.overlay

internal object ActorIdentity {
    // Active ExpertId, not cosmetic HeroId or the pawn's default model.
    // Only UI-verified bindings belong here. Class-kit IDs and cosmetic IDs
    // cannot be assigned operator names from a guessed numbering scheme.
    private val operators = mapOf(
        10001L to "Stinger", 10002L to "Vlinder", 10003L to "Rover",
        10004L to "Toxik", 10005L to "Branko",
        20001L to "Luna", 20002L to "Raptor", 20003L to "Hackclaw",
        20004L to "Morse",
        30001L to "D-Wolf", 30002L to "Vyron", 30003L to "Martelo",
        30004L to "Tempest", 30005L to "Nox",
        40001L to "Shepherd", 40002L to "Uluru", 40003L to "Sineva",
        40004L to "Gizmo", 40005L to "N-Two"
    )

    fun label(kind: String, verified: Boolean, operatorId: Long, template: String): String {
        if (kind == "l") return ""
        if (!verified) return if (kind == "a") "Unknown bot" else "Unknown operator"
        if (kind != "a" || operatorId > 0) {
            val operator = operators[operatorId] ?: if (operatorId > 0) "Operator $operatorId" else "Unknown operator"
            return operator + if (kind == "a") " bot" else ""
        }
        if (!template.matches(Regex("[A-Za-z0-9_-]{1,95}"))) return "Unknown bot"
        val tokens = template.split('_', '-').filter { it.isNotBlank() }.toMutableList()
        while (tokens.firstOrNull()?.uppercase() in listOf("SOL", "DT", "BT", "AI", "NPC")) tokens.removeAt(0)
        if (tokens.isEmpty()) return "Unknown bot"
        return tokens.joinToString(" ") {
            when (it.uppercase()) {
                "BASIC", "NORMAL" -> "Regular"
                "SHIELDER" -> "Shield"
                "ANIMAL" -> ""
                "AR", "SMG", "LMG", "DMR" -> it.uppercase()
                else -> it.replace(Regex("(?i)^boss(\\d+)$"), "Boss $1")
                    .lowercase().replaceFirstChar { c -> c.uppercase() }
            }
        }.trim().replace(Regex(" +"), " ").take(64)
    }

    fun labelText(name: String, type: String, showName: Boolean, showType: Boolean): String =
        listOfNotNull(name.takeIf { showName }, type.takeIf { showType && it.isNotBlank() })
            .joinToString(" · ")
}
