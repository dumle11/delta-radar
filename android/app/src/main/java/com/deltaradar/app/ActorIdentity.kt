package com.deltaradar.app

internal object ActorIdentity {
    // Active ExpertId, not cosmetic HeroId or the pawn's default model.
    private val operators = mapOf(
        10010L to "Stinger", 10020L to "Toxik",
        20010L to "Luna", 20020L to "Hackclaw",
        30010L to "D-Wolf", 30020L to "Vyron",
        40010L to "Shepherd", 40020L to "Uluru"
    )

    fun label(kind: String, verified: Boolean, operatorId: Long, template: String): String {
        if (kind == "loot") return ""
        if (!verified) return if (kind == "ai") "Unknown bot" else "Unknown operator"
        if (kind != "ai" || operatorId > 0) {
            val operator = operators[operatorId] ?: if (operatorId > 0) "Operator $operatorId" else "Unknown operator"
            return operator + if (kind == "ai") " bot" else ""
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
