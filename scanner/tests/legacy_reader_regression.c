/* Exercises the historical reader against its own synthetic process memory. */
#define main scanner_main
#include "../memscan-legacy.c"
#undef main
#include <assert.h>

static void put_ptr(unsigned char *base, size_t offset, const void *value) {
    uint64_t pointer = (uintptr_t)value;
    memcpy(base + offset, &pointer, sizeof(pointer));
}

static void put_i32(unsigned char *base, size_t offset, int32_t value) {
    memcpy(base + offset, &value, sizeof(value));
}

static void put_float(unsigned char *base, size_t offset, float value) {
    memcpy(base + offset, &value, sizeof(value));
}

static Entity *find_entity(const void *actor) {
    char id[32];
    snprintf(id, sizeof(id), "ent_%llx", (unsigned long long)(uintptr_t)actor);
    for (int i = 0; i < num_entities; i++)
        if (strcmp(entities[i].id, id) == 0) return &entities[i];
    return NULL;
}

int main(void) {
    game_pid = getpid();
    use_vm_readv = 1;
    ue4_base = 0x7e00000000ULL;
    ue4_end = ue4_base + 0x100000;
    assert(!is_game_heap_ptr(0x7cded20020ULL));
    assert(is_telemetry_ptr(0x7cded20020ULL));
    assert(is_telemetry_ptr(0x7d64859070ULL));
    assert(!is_telemetry_ptr(ue4_base + 8));
    assert(!is_telemetry_ptr(0));
    assert(!is_telemetry_ptr(0x7cded20021ULL));

    _Alignas(8) unsigned char world[0x300] = {0}, level[0x200] = {0};
    _Alignas(8) unsigned char game_instance[0x100] = {0}, local_player[0x100] = {0};
    _Alignas(8) unsigned char controller[0x900] = {0}, camera[0x5000] = {0};
    _Alignas(8) unsigned char pawn[0x2000] = {0}, root[0x600] = {0};
    _Alignas(8) unsigned char player_state[0x700] = {0}, pawn_class[0x100] = {0};
    _Alignas(8) unsigned char enemy[0x2000] = {0}, enemy_root[0x600] = {0};
    _Alignas(8) unsigned char enemy_state[0x700] = {0}, enemy_class[0x100] = {0};
    _Alignas(8) unsigned char bot[0x2000] = {0}, bot_root[0x600] = {0};
    _Alignas(8) unsigned char bot_state[0x700] = {0};
    _Alignas(8) unsigned char teammate[0x2000] = {0}, teammate_root[0x600] = {0};
    _Alignas(8) unsigned char teammate_state[0x700] = {0};
    _Alignas(8) unsigned char loot[0x2000] = {0}, loot_root[0x600] = {0};
    _Alignas(8) unsigned char loot_class[0x100] = {0};
    _Alignas(8) uint16_t name[] = {'T', 'e', 's', 't', 0};
    _Alignas(8) uint16_t enemy_name[] = {'E', 'n', 'e', 'm', 'y', 0};
    _Alignas(8) uint16_t bot_name[] = {'B', 'o', 't', 0};
    _Alignas(8) uint16_t teammate_name[] = {'A', 'l', 'l', 'y', 0};
    uint64_t local_players[] = {(uintptr_t)local_player};
    uint64_t actors[] = {
        (uintptr_t)pawn, (uintptr_t)enemy, (uintptr_t)bot,
        (uintptr_t)teammate, (uintptr_t)loot
    };
    uint64_t world_ptr = (uintptr_t)world;

    put_ptr(world, OFF_UWORLD_PERSISTENT_LEVEL, level);
    put_ptr(world, 0x190, game_instance);
    put_ptr(level, 0x98, actors);
    put_i32(level, 0xa0, 5);
    put_ptr(game_instance, 0x38, local_players);
    put_ptr(local_player, 0x30, controller);
    put_ptr(controller, OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN, pawn);
    put_ptr(controller, OFF_PC_CAMERA_MANAGER, camera);
    put_ptr(camera, 0x370, controller);
    put_float(camera, 0x388, 90.0f);
    put_ptr(pawn, OFF_UOBJECT_CLASS, pawn_class);
    put_ptr(pawn, OFF_AACTOR_ROOT_COMPONENT, root);
    put_ptr(pawn, OFF_APAWN_PLAYER_STATE, player_state);
    put_ptr(player_state, OFF_PLAYER_STATE_NAME, name);
    put_i32(player_state, OFF_PLAYER_STATE_NAME + 8, 5);
    put_i32(player_state, OFF_PLAYER_STATE_NAME + 12, 5);
    put_i32(player_state, OFF_GP_PLAYER_STATE_TEAM_ID, 4);
    put_ptr(player_state, OFF_PLAYER_STATE_PAWN_PRIVATE, pawn);
    put_ptr(enemy, OFF_UOBJECT_CLASS, enemy_class);
    put_ptr(enemy, OFF_AACTOR_ROOT_COMPONENT, enemy_root);
    put_ptr(enemy, OFF_APAWN_PLAYER_STATE, enemy_state);
    put_ptr(enemy_state, OFF_PLAYER_STATE_PAWN_PRIVATE, enemy);
    put_ptr(enemy_state, OFF_PLAYER_STATE_NAME, enemy_name);
    put_i32(enemy_state, OFF_PLAYER_STATE_NAME + 8, 6);
    put_i32(enemy_state, OFF_PLAYER_STATE_NAME + 12, 6);
    put_i32(enemy_state, OFF_GP_PLAYER_STATE_TEAM_ID, 7);
    put_float(enemy_root, 0x148, 10500.0f);
    put_float(enemy_root, OFF_CAPSULE_COMPONENT_HALF_HEIGHT, 100.0f);
    put_ptr(bot, OFF_UOBJECT_CLASS, pawn_class);
    put_ptr(bot, OFF_AACTOR_ROOT_COMPONENT, bot_root);
    put_ptr(bot, OFF_APAWN_PLAYER_STATE, bot_state);
    put_ptr(bot_state, OFF_PLAYER_STATE_PAWN_PRIVATE, bot);
    put_ptr(bot_state, OFF_PLAYER_STATE_NAME, bot_name);
    put_i32(bot_state, OFF_PLAYER_STATE_NAME + 8, 4);
    put_i32(bot_state, OFF_PLAYER_STATE_NAME + 12, 4);
    bot_state[OFF_PLAYER_STATE_BOT_FLAGS] = 0x08;
    put_i32(bot_state, OFF_GP_PLAYER_STATE_TEAM_ID, 7);
    put_float(bot_root, 0x148, 11000.0f);
    put_ptr(teammate, OFF_UOBJECT_CLASS, pawn_class);
    put_ptr(teammate, OFF_AACTOR_ROOT_COMPONENT, teammate_root);
    put_ptr(teammate, OFF_APAWN_PLAYER_STATE, teammate_state);
    put_ptr(teammate_state, OFF_PLAYER_STATE_PAWN_PRIVATE, teammate);
    put_ptr(teammate_state, OFF_PLAYER_STATE_NAME, teammate_name);
    put_i32(teammate_state, OFF_PLAYER_STATE_NAME + 8, 5);
    put_i32(teammate_state, OFF_PLAYER_STATE_NAME + 12, 5);
    put_i32(teammate_state, OFF_GP_PLAYER_STATE_TEAM_ID, 4);
    put_float(teammate_root, 0x148, 11500.0f);
    put_ptr(loot, OFF_UOBJECT_CLASS, loot_class);
    put_ptr(loot, OFF_AACTOR_ROOT_COMPONENT, loot_root);
    put_float(loot_root, 0x148, 12000.0f);
    put_float(loot_root, OFF_CAPSULE_COMPONENT_HALF_HEIGHT, 100.0f);
    put_float(root, 0x148, 10000.0f);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE, 1.0f);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE + OFF_CAMERA_CACHE_POV, 10000.0f);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE + OFF_CAMERA_CACHE_POV + OFF_POV_FOV, 90.0f);
    resolved_mesh_off = -1;
    read_buf = malloc(READ_BUF_SIZE);
    assert(read_buf);

    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(!self_verified);
    assert(resolved_camera_manager_off == OFF_PC_CAMERA_MANAGER);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE, 2.0f);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(self_verified);
    assert(resolved_camera_cache_off == 0x2d60);
    assert(strcmp(entities[0].name, "Test") == 0);
    Entity *remote = find_entity(enemy);
    Entity *ai = find_entity(bot);
    Entity *ally = find_entity(teammate);
    assert(remote && strcmp(remote->kind, "player") == 0 && remote->team == 2);
    assert(strcmp(remote->name, "Enemy") == 0 && remote->native_team == 7);
    assert(ai && strcmp(ai->kind, "ai") == 0 && ai->team == 2);
    assert(ally && strcmp(ally->kind, "player") == 0 && ally->team == 1);
    assert(find_entity(loot) == NULL);
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    persistent_sock = sockets[0];
    assert(post_to_radar() == 0);
    char snapshot[8192] = {0};
    ssize_t received = recv(sockets[1], snapshot, sizeof(snapshot) - 1, 0);
    assert(received > 0 && snapshot[received - 1] == '\n');
    char player_prefix[80];
    snprintf(player_prefix, sizeof(player_prefix), "\"id\":\"%s\",\"kind\":\"player\"", remote->id);
    char *player_json = strstr(snapshot, player_prefix);
    assert(player_json);
    char *player_end = strchr(player_json, '}');
    char *player_name = strstr(player_json, "\"name\":\"Enemy\"");
    char *player_team = strstr(player_json, "\"team\":2");
    assert(player_end && player_name && player_name < player_end);
    assert(player_team && player_team < player_end);
    close_radar_sock();
    close(sockets[1]);

    put_ptr(enemy_state, OFF_PLAYER_STATE_PAWN_PRIVATE, bot);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 3);
    assert(find_entity(enemy) == NULL);
    put_ptr(enemy_state, OFF_PLAYER_STATE_PAWN_PRIVATE, enemy);
    enemy[OFF_CHARACTER_LAST_IS_DEAD] = 1;
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(find_entity(enemy) && find_entity(enemy)->dead);
    enemy[OFF_CHARACTER_LAST_IS_DEAD] = 0;
    put_i32(player_state, OFF_PLAYER_STATE_NAME + 8, 0);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(!self_verified);
    put_i32(player_state, OFF_PLAYER_STATE_NAME + 8, 5);
    for (int i = 0; i < 48; i++)
        assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(!self_verified);
    free(read_buf);
    puts("legacy reader regression checks passed");
    return 0;
}
