package com.deltaradar.app

import android.content.Context
import android.graphics.Color

object Settings {
    private const val PREFS = "delta_radar"
    private const val LINES_KEY = "draw_enemy_lines"
    private const val LABELS_KEY = "show_enemy_labels"
    private const val SHOW_PLAYERS_KEY = "show_players"
    private const val SHOW_TEAMMATES_KEY = "show_teammates"
    private const val SHOW_LOOT_KEY = "show_loot"
    private const val RENDER_DIST_KEY = "render_distance"
    private const val PLAYER_COLOR_KEY = "player_color"
    private const val BOT_COLOR_KEY = "bot_color"
    private const val PLAYER_DRAW_MODE_KEY = "player_draw_mode"
    private const val BOT_DRAW_MODE_KEY = "bot_draw_mode"
    private const val TEAMMATE_COLOR_KEY = "teammate_color"
    private const val TEAMMATE_DRAW_MODE_KEY = "teammate_draw_mode"
    private const val LOOT_COLOR_KEY = "loot_color"
    private const val LOOT_DRAW_MODE_KEY = "loot_draw_mode"
    private const val LINE_THICKNESS_KEY = "line_thickness_percent"
    private const val PLAYER_FEATURES_KEY = "player_features_v4"
    private const val BOT_FEATURES_KEY = "bot_features_v4"
    private const val TEAMMATE_FEATURES_KEY = "teammate_features_v4"
    private const val LOOT_FEATURES_KEY = "loot_features_v4"

    const val DEFAULT_PLAYER_COLOR: Int = 0xFFE5534B.toInt()
    const val DEFAULT_BOT_COLOR: Int = 0xFF46D9FF.toInt()
    const val DEFAULT_TEAMMATE_COLOR: Int = 0xFF4FD08A.toInt()
    const val LOOT_COLOR: Int = 0xFF4BCA81.toInt()

    const val DRAW_MODE_OFF = -1
    const val DRAW_MODE_LINES = 0
    const val DRAW_MODE_BOXES = 1
    const val DRAW_MODE_SKELETON = 2

    const val FEATURE_ENABLED = 1
    const val FEATURE_LINES = 1 shl 1
    const val FEATURE_NAMES = 1 shl 2
    const val FEATURE_DISTANCE = 1 shl 3
    const val FEATURE_HEALTH = 1 shl 4
    const val FEATURE_BODY = 1 shl 5
    const val ALL_FEATURES = FEATURE_ENABLED or FEATURE_LINES or FEATURE_NAMES or
        FEATURE_DISTANCE or FEATURE_HEALTH or FEATURE_BODY

    enum class EntityType { PLAYER, BOT, TEAMMATE, LOOT }

    private fun prefs(ctx: Context) = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE)

    fun aimEnabled(ctx: Context) = prefs(ctx).getBoolean("aim_enabled", false)
    fun setAimEnabled(ctx: Context, value: Boolean) { prefs(ctx).edit().putBoolean("aim_enabled", value).apply() }
    fun aimCircle(ctx: Context) = prefs(ctx).getBoolean("aim_circle", true)
    fun setAimCircle(ctx: Context, value: Boolean) { prefs(ctx).edit().putBoolean("aim_circle", value).apply() }
    fun aimFov(ctx: Context) = prefs(ctx).getInt("aim_fov", 15).coerceIn(5, 45)
    fun setAimFov(ctx: Context, value: Int) { prefs(ctx).edit().putInt("aim_fov", value.coerceIn(5, 45)).apply() }
    fun aimStrength(ctx: Context) = prefs(ctx).getInt("aim_strength", 30).coerceIn(10, 60)
    fun setAimStrength(ctx: Context, value: Int) { prefs(ctx).edit().putInt("aim_strength", value.coerceIn(10, 60)).apply() }
    fun autoShoot(ctx: Context) = prefs(ctx).getBoolean("auto_shoot", false)
    fun setAutoShoot(ctx: Context, value: Boolean) { prefs(ctx).edit().putBoolean("auto_shoot", value).apply() }
    fun aimLookPoint(ctx: Context) = prefs(ctx).let {
        AimMath.lookPointOrDefault(it.getFloat("aim_look_x", .7f), it.getFloat("aim_look_y", .55f))
    }
    fun aimFirePoint(ctx: Context): Pair<Float, Float>? = prefs(ctx).let {
        if (!it.contains("aim_fire_x")) null else it.getFloat("aim_fire_x", 0f) to it.getFloat("aim_fire_y", 0f)
    }
    fun resetAimLookPoint(ctx: Context) {
        prefs(ctx).edit().remove("aim_look_x").remove("aim_look_y").apply()
    }
    fun setAimTouchPoint(ctx: Context, fire: Boolean, x: Float, y: Float) {
        val key = if (fire) "aim_fire" else "aim_look"
        prefs(ctx).edit().putFloat("${key}_x", x.coerceIn(.02f, .98f))
            .putFloat("${key}_y", y.coerceIn(.02f, .98f)).apply()
    }

    fun drawEnemyLines(ctx: Context) = prefs(ctx).getBoolean(LINES_KEY, false)
    fun setDrawEnemyLines(ctx: Context, v: Boolean) { prefs(ctx).edit().putBoolean(LINES_KEY, v).apply() }

    fun showEnemyLabels(ctx: Context) = prefs(ctx).getBoolean(LABELS_KEY, false)
    fun setShowEnemyLabels(ctx: Context, v: Boolean) { prefs(ctx).edit().putBoolean(LABELS_KEY, v).apply() }

    fun showPlayers(ctx: Context) = prefs(ctx).getBoolean(SHOW_PLAYERS_KEY, true)
    fun setShowPlayers(ctx: Context, v: Boolean) { prefs(ctx).edit().putBoolean(SHOW_PLAYERS_KEY, v).apply() }

    fun showTeammates(ctx: Context) = prefs(ctx).getBoolean(SHOW_TEAMMATES_KEY, false)
    fun setShowTeammates(ctx: Context, v: Boolean) { prefs(ctx).edit().putBoolean(SHOW_TEAMMATES_KEY, v).apply() }

    fun showLoot(ctx: Context) = prefs(ctx).getBoolean(SHOW_LOOT_KEY, false)
    fun setShowLoot(ctx: Context, v: Boolean) { prefs(ctx).edit().putBoolean(SHOW_LOOT_KEY, v).apply() }

    fun renderDistance(ctx: Context) = prefs(ctx).getInt(RENDER_DIST_KEY, 500)
    fun setRenderDistance(ctx: Context, v: Int) { prefs(ctx).edit().putInt(RENDER_DIST_KEY, v).apply() }

    fun playerColor(ctx: Context): Int = prefs(ctx).getInt(PLAYER_COLOR_KEY, DEFAULT_PLAYER_COLOR) or 0xFF000000.toInt()
    fun setPlayerColor(ctx: Context, color: Int) { prefs(ctx).edit().putInt(PLAYER_COLOR_KEY, color or 0xFF000000.toInt()).apply() }

    fun botColor(ctx: Context): Int = prefs(ctx).getInt(BOT_COLOR_KEY, DEFAULT_BOT_COLOR) or 0xFF000000.toInt()
    fun setBotColor(ctx: Context, color: Int) { prefs(ctx).edit().putInt(BOT_COLOR_KEY, color or 0xFF000000.toInt()).apply() }

    fun playerDrawMode(ctx: Context) = prefs(ctx).getInt(PLAYER_DRAW_MODE_KEY, DRAW_MODE_BOXES)
    fun setPlayerDrawMode(ctx: Context, v: Int) { prefs(ctx).edit().putInt(PLAYER_DRAW_MODE_KEY, v).apply() }

    fun botDrawMode(ctx: Context) = prefs(ctx).getInt(BOT_DRAW_MODE_KEY, DRAW_MODE_BOXES)
    fun setBotDrawMode(ctx: Context, v: Int) { prefs(ctx).edit().putInt(BOT_DRAW_MODE_KEY, v).apply() }

    fun teammateColor(ctx: Context): Int = prefs(ctx).getInt(TEAMMATE_COLOR_KEY, DEFAULT_TEAMMATE_COLOR) or 0xFF000000.toInt()
    fun setTeammateColor(ctx: Context, color: Int) { prefs(ctx).edit().putInt(TEAMMATE_COLOR_KEY, color or 0xFF000000.toInt()).apply() }

    fun teammateDrawMode(ctx: Context) = prefs(ctx).getInt(TEAMMATE_DRAW_MODE_KEY, DRAW_MODE_BOXES)
    fun setTeammateDrawMode(ctx: Context, v: Int) { prefs(ctx).edit().putInt(TEAMMATE_DRAW_MODE_KEY, v).apply() }

    fun lootColor(ctx: Context): Int = prefs(ctx).getInt(LOOT_COLOR_KEY, LOOT_COLOR) or 0xFF000000.toInt()
    fun setLootColor(ctx: Context, color: Int) { prefs(ctx).edit().putInt(LOOT_COLOR_KEY, color or 0xFF000000.toInt()).apply() }

    fun lootDrawMode(ctx: Context) = prefs(ctx).getInt(LOOT_DRAW_MODE_KEY, DRAW_MODE_BOXES)
    fun setLootDrawMode(ctx: Context, v: Int) { prefs(ctx).edit().putInt(LOOT_DRAW_MODE_KEY, v).apply() }

    fun lineThicknessPercent(ctx: Context) = prefs(ctx).getInt(LINE_THICKNESS_KEY, 75).coerceIn(25, 300)
    fun setLineThicknessPercent(ctx: Context, v: Int) { prefs(ctx).edit().putInt(LINE_THICKNESS_KEY, v.coerceIn(25, 300)).apply() }

    fun healthBarPosition(ctx: Context) = prefs(ctx).getInt("health_bar_position", HealthBarLayout.LEFT).coerceIn(0, 3)
    fun setHealthBarPosition(ctx: Context, v: Int) { prefs(ctx).edit().putInt("health_bar_position", v.coerceIn(0, 3)).apply() }
    fun healthBarThicknessDp(ctx: Context) = prefs(ctx).getFloat("health_bar_thickness_dp", .75f).coerceIn(.5f, 4f)
    fun setHealthBarThicknessDp(ctx: Context, v: Float) { prefs(ctx).edit().putFloat("health_bar_thickness_dp", v.coerceIn(.5f, 4f)).apply() }

    private fun featureKey(type: EntityType) = when (type) {
        EntityType.PLAYER -> PLAYER_FEATURES_KEY
        EntityType.BOT -> BOT_FEATURES_KEY
        EntityType.TEAMMATE -> TEAMMATE_FEATURES_KEY
        EntityType.LOOT -> LOOT_FEATURES_KEY
    }

    private fun legacyFeatureKey(type: EntityType, version: Int) = when (type) {
        EntityType.PLAYER -> "player_features_v$version"
        EntityType.BOT -> "bot_features_v$version"
        EntityType.TEAMMATE -> "teammate_features_v$version"
        EntityType.LOOT -> "loot_features_v$version"
    }

    private fun drawMode(ctx: Context, type: EntityType) = when (type) {
        EntityType.PLAYER -> playerDrawMode(ctx)
        EntityType.BOT -> botDrawMode(ctx)
        EntityType.TEAMMATE -> teammateDrawMode(ctx)
        EntityType.LOOT -> lootDrawMode(ctx)
    }

    fun entityFeatures(ctx: Context, type: EntityType): Int {
        val p = prefs(ctx)
        val key = featureKey(type)
        if (p.contains(key)) return p.getInt(key, 0)
        for (version in 3 downTo 2) {
            val legacyKey = legacyFeatureKey(type, version)
            if (p.contains(legacyKey)) {
                var migrated = p.getInt(legacyKey, 0) and FEATURE_BODY.inv()
                if (drawMode(ctx, type) > DRAW_MODE_LINES) migrated = migrated or FEATURE_BODY
                p.edit().putInt(key, migrated).apply()
                return migrated
            }
        }
        // Migrate the previous global switches without surprising existing
        // users. Names and distance used to share one label switch.
        var mask = 0
        val enabled = when (type) {
            EntityType.TEAMMATE -> showTeammates(ctx)
            EntityType.LOOT -> showLoot(ctx)
            else -> showPlayers(ctx)
        }
        if (enabled) mask = mask or FEATURE_ENABLED
        if (drawEnemyLines(ctx)) mask = mask or FEATURE_LINES
        if (showEnemyLabels(ctx)) mask = mask or FEATURE_NAMES or FEATURE_DISTANCE
        if (type != EntityType.LOOT) mask = mask or FEATURE_HEALTH
        if (drawMode(ctx, type) > DRAW_MODE_LINES) mask = mask or FEATURE_BODY
        p.edit().putInt(key, mask).apply()
        return mask
    }

    fun setEntityFeatures(ctx: Context, type: EntityType, mask: Int) {
        prefs(ctx).edit().putInt(featureKey(type), mask and ALL_FEATURES).apply()
        if (type == EntityType.LOOT)
            setShowLoot(ctx, mask and FEATURE_ENABLED != 0)
    }

    fun entityFeature(ctx: Context, type: EntityType, feature: Int) =
        entityFeatures(ctx, type) and feature != 0

    fun setEntityFeature(ctx: Context, type: EntityType, feature: Int, enabled: Boolean) {
        val old = entityFeatures(ctx, type)
        setEntityFeatures(ctx, type, if (enabled) old or feature else old and feature.inv())
    }
}
