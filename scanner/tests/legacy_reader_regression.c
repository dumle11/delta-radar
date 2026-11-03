/* Exercises the historical reader against its own synthetic process memory. */
#define main scanner_main
#include "../memscan-legacy.c"
#undef main
#include <assert.h>
#include <sys/wait.h>

static void put_ptr(unsigned char *base, size_t offset, const void *value) {
    uint64_t pointer = (uintptr_t)value;
    memcpy(base + offset, &pointer, sizeof(pointer));
}

static void put_i32(unsigned char *base, size_t offset, int32_t value) {
    memcpy(base + offset, &value, sizeof(value));
}

static void put_u16(unsigned char *base, size_t offset, uint16_t value) {
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

static void test_large_snapshot(void) {
    memset(entities, 0, sizeof(entities));
    num_entities = MAX_ACTORS;
    for (int i = 0; i < num_entities; i++) {
        Entity *entity = &entities[i];
        snprintf(entity->id, sizeof(entity->id), "ent_%x", i);
        strcpy(entity->kind, "player");
        memset(entity->name, '"', sizeof(entity->name) - 1);
        memset(entity->class_name, '\\', sizeof(entity->class_name) - 1);
        entity->position_observation.present = 1;
        entity->position_observation.bounds_index = 65535;
        entity->position_observation.world_index = -1;
        entity->has_bones = 1;
        for (int b = 0; b < NUM_SKELETON_BONES; b++)
            for (int c = 0; c < 3; c++) entity->bone_pos[b][c] = 100000.0f;
    }
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        close(sockets[0]);
        char snapshot[131073];
        size_t used = 0;
        ssize_t n;
        while (used < sizeof(snapshot) - 1 &&
               (n = read(sockets[1], snapshot + used, sizeof(snapshot) - 1 - used)) > 0)
            used += (size_t)n;
        snapshot[used] = 0;
        int valid = used > 0 && snapshot[used - 1] == '\n' &&
            strstr(snapshot, "\"positionTrace\":") &&
            strstr(snapshot, "],\"entitiesTruncated\":true}\n");
        close(sockets[1]);
        _exit(valid ? 0 : 1);
    }
    close(sockets[1]);
    persistent_sock = sockets[0];
    assert(post_to_radar() == 0);
    close_radar_sock();
    int status;
    assert(waitpid(child, &status, 0) == child);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    num_entities = 0;
}

static void test_character_identity(void) {
    _Alignas(8) unsigned char object[0x80] = {0}, actor[0x80] = {0};
    _Alignas(8) unsigned char pawn[0x80] = {0}, character[0x80] = {0};
    _Alignas(8) unsigned char local[0x80] = {0}, prop[0x80] = {0}, bot[0x80] = {0};
    put_i32(object, 0x24, 1);
    put_i32(actor, 0x24, 57); put_ptr(actor, 0x40, object);
    put_i32(pawn, 0x24, 0xD5); put_ptr(pawn, 0x40, actor);
    put_i32(character, 0x24, 0xD7); put_ptr(character, 0x40, pawn);
    put_ptr(local, 0x40, pawn); /* lobby pawn: shares Actor/Object with props */
    put_ptr(prop, 0x40, actor); put_ptr(bot, 0x40, character);
    assert(!is_character_class((uintptr_t)prop, (uintptr_t)local));
    assert(!is_character_class((uintptr_t)local, (uintptr_t)local));
    assert(is_character_class((uintptr_t)bot, (uintptr_t)local));
    assert(is_character_class((uintptr_t)bot, 0));
    put_ptr(character, 0x40, actor); /* wrong parent cannot spoof native index */
    assert(!is_character_class((uintptr_t)bot, (uintptr_t)local));
    put_ptr(prop, 0x40, prop); /* bounded on cyclic/corrupt metadata */
    assert(!is_character_class((uintptr_t)prop, (uintptr_t)local));
}

static void test_sdk_world_admission(void) {
    _Alignas(8) unsigned char world[0x200] = {0}, level[0x200] = {0}, gs[0x400] = {0};
    _Alignas(8) unsigned char gi[0x100] = {0}, lp[0x50] = {0}, pc[0x500] = {0};
    _Alignas(8) unsigned char camera[0x400] = {0}, state[0x500] = {0};
    _Alignas(8) uint16_t name[] = {'T', 'e', 's', 't', 0};
    uint64_t slot = (uintptr_t)world, locals[] = {(uintptr_t)lp};
    uint64_t states[] = {(uintptr_t)state}, actors[] = {(uintptr_t)pc};
    put_ptr(world, 0xF8, level); put_ptr(world, 0x140, gs); put_ptr(world, 0x190, gi);
    put_ptr(level, 0xA8, world); put_ptr(level, 0x98, actors);
    put_i32(level, 0xA0, 1); put_i32(level, 0xA4, 1);
    put_ptr(gs, 0x388, states); put_i32(gs, 0x390, 1);
    put_ptr(gi, 0x38, locals); put_ptr(lp, 0x30, pc); put_ptr(pc, 0x408, camera);
    put_ptr(camera, 0x370, pc);
    GWorldCandidate candidate;
    assert(!select_sdk_world((uintptr_t)&slot, &candidate)); /* unnamed preview */
    put_ptr(state, OFF_PLAYER_STATE_NAME, name);
    put_i32(state, OFF_PLAYER_STATE_NAME + 8, 5); put_i32(state, OFF_PLAYER_STATE_NAME + 12, 5);
    assert(select_sdk_world((uintptr_t)&slot, &candidate));
    assert(candidate.gworld_addr == (uintptr_t)&slot && candidate.actor_source == 1);
    /* Camera ownership and the level/world relationship are required. */
    put_ptr(camera, 0x370, state);
    assert(!select_sdk_world((uintptr_t)&slot, &candidate));
    put_ptr(camera, 0x370, pc); put_ptr(level, 0xA8, gs);
    assert(!select_sdk_world((uintptr_t)&slot, &candidate));
}

static void test_aim_metadata(void) {
    _Alignas(8) unsigned char actor[0x400] = {0}, mesh[0x900] = {0}, root[0x300] = {0};
    float position[3]; int plaintext = 0;
    put_float(root, 0x148, 10000);
    put_float(root, 0x21C, 1); /* world quaternion W */
    put_float(root, 0x220, 10000);
    put_u16(root, 0x154, UINT16_MAX);
    put_u16(root, 0x238, UINT16_MAX);
    PositionObservation observation;
    assert(read_actor_position((uintptr_t)root, 0x148, position, &plaintext, &observation) && plaintext);
    assert(observation.present && observation.root == (uintptr_t)root);
    assert(observation.bounds_index == UINT16_MAX && observation.world_index == UINT16_MAX);
    put_u16(root, 0x154, 0); /* active slot with bEncrypted clear still needs decoding */
    assert(read_actor_position((uintptr_t)root, 0x148, position, &plaintext, &observation) && !plaintext);
    put_u16(root, 0x154, UINT16_MAX);
    put_u16(root, 0x238, 4105);
    assert(read_actor_position((uintptr_t)root, 0x148, position, &plaintext, &observation) && !plaintext);
    put_u16(root, 0x238, UINT16_MAX);
    put_float(root, 0x220, 10100); /* finite bounds at the wrong location */
    assert(read_actor_position((uintptr_t)root, 0x148, position, &plaintext, &observation) && !plaintext);
    put_float(root, 0x220, 10000);
    root[0x23A] = 1; /* bounds clear, transform encrypted */
    assert(read_actor_position((uintptr_t)root, 0x148, position, &plaintext, &observation) && !plaintext);
    root[0x23A] = 0;
    root[0x156] = 1;
    assert(read_actor_position((uintptr_t)root, 0x148, position, &plaintext, &observation) && !plaintext);
    root[0x156] = 0;
    assert(read_actor_position((uintptr_t)root, 0x164, position, &plaintext, &observation) && !plaintext);
    assert(observation.bounds_index == -1 && observation.world_index == -1);
    assert(!read_actor_position(0, 0x148, position, &plaintext, &observation) && !plaintext);
    assert(!actor_rendered_on_screen((uintptr_t)actor));
    put_ptr(actor, 0x3D0, mesh);
    assert(!actor_rendered_on_screen((uintptr_t)actor));
    mesh[0x8B8] = 1;
    assert(actor_rendered_on_screen((uintptr_t)actor));
    mesh[0x8B8] = 2;
    assert(!actor_rendered_on_screen((uintptr_t)actor));
}

static void test_loot_discovery(void) {
    _Alignas(8) unsigned char world[0x300] = {0}, level[0x200] = {0}, streamed[0x200] = {0};
    _Alignas(8) unsigned char gi[0x100] = {0}, lp[0x100] = {0}, pc[0x900] = {0};
    _Alignas(8) unsigned char camera[0x5000] = {0};
    _Alignas(8) unsigned char pawn[0x2000] = {0}, root[0x600] = {0}, state[0x700] = {0};
    _Alignas(8) unsigned char pawn_class[0x100] = {0};
    _Alignas(8) unsigned char item[0x2000] = {0}, container[0x2000] = {0}, box[0x2000] = {0};
    _Alignas(8) unsigned char decoy[0x2000] = {0}, decoy_state[0x700] = {0};
    _Alignas(8) unsigned char item_root[0x600] = {0}, container_root[0x600] = {0}, box_root[0x600] = {0};
    _Alignas(8) unsigned char pickup_class[0x100] = {0}, pickup_parent[0x100] = {0};
    _Alignas(8) unsigned char container_class[0x100] = {0}, box_class[0x100] = {0}, blueprint[0x100] = {0};
    _Alignas(8) unsigned char invalid_class[0x100] = {0};
    _Alignas(8) uint16_t name[] = {'T', 'e', 's', 't', 0};
    uint64_t local_players[] = {(uintptr_t)lp};
    uint64_t actors[] = {(uintptr_t)pawn, (uintptr_t)decoy};
    uint64_t other_actors[] = {(uintptr_t)item, (uintptr_t)container, (uintptr_t)box,
                               (uintptr_t)item};
    uint64_t levels[128] = {(uintptr_t)streamed};
    uint64_t world_ptr = (uintptr_t)world;

    put_ptr(world, OFF_UWORLD_PERSISTENT_LEVEL, level);
    put_ptr(world, OFF_UWORLD_LEVELS, levels);
    put_i32(world, OFF_UWORLD_LEVELS + 8, 128);
    put_ptr(world, 0x190, gi);
    put_ptr(level, 0x98, actors); put_i32(level, 0xa0, 2);
    put_ptr(streamed, 0x98, other_actors); put_i32(streamed, 0xa0, 4);
    put_ptr(gi, 0x38, local_players); put_ptr(lp, 0x30, pc);
    put_ptr(pc, OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN, pawn);
    put_ptr(pc, OFF_PC_CAMERA_MANAGER, camera);
    put_ptr(camera, 0x370, pc); put_float(camera, 0x388, 90);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE, 1);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE + OFF_CAMERA_CACHE_POV, 10000);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE + OFF_CAMERA_CACHE_POV + OFF_POV_FOV, 105);
    put_ptr(pawn, OFF_UOBJECT_CLASS, pawn_class);
    put_ptr(pawn, OFF_AACTOR_ROOT_COMPONENT, root);
    put_ptr(pawn, OFF_APAWN_PLAYER_STATE, state);
    put_ptr(state, OFF_PLAYER_STATE_NAME, name);
    put_i32(state, OFF_PLAYER_STATE_NAME + 8, 5);
    put_i32(state, OFF_PLAYER_STATE_NAME + 12, 5);
    put_ptr(state, OFF_PLAYER_STATE_PAWN_PRIVATE, pawn);
    put_float(root, 0x148, 10000);

    put_i32(pickup_class, 0x24, 7824); put_ptr(pickup_class, 0x40, pickup_parent);
    put_i32(pickup_parent, 0x24, 7823);
    put_i32(container_class, 0x24, 7825); put_ptr(container_class, 0x40, pickup_class);
    put_i32(box_class, 0x24, 7826); put_ptr(box_class, 0x40, container_class);
    put_i32(blueprint, 0x24, 300000); put_ptr(blueprint, 0x40, box_class);
    put_i32(invalid_class, 0x24, 7825); put_ptr(invalid_class, 0x40, pawn_class);
    assert(loot_kind_from_class((uintptr_t)invalid_class) == LOOT_NONE);
    put_ptr(item, OFF_UOBJECT_CLASS, pickup_class); put_ptr(item, OFF_AACTOR_ROOT_COMPONENT, item_root);
    put_ptr(container, OFF_UOBJECT_CLASS, container_class); put_ptr(container, OFF_AACTOR_ROOT_COMPONENT, container_root);
    put_ptr(box, OFF_UOBJECT_CLASS, blueprint); put_ptr(box, OFF_AACTOR_ROOT_COMPONENT, box_root);
    put_ptr(decoy, OFF_UOBJECT_CLASS, pickup_class);
    put_ptr(decoy, OFF_APAWN_PLAYER_STATE, decoy_state);
    put_ptr(decoy_state, OFF_PLAYER_STATE_PAWN_PRIVATE, decoy);
    unsigned char *roots[] = {item_root, container_root, box_root};
    for (int i = 0; i < 3; i++) {
        put_float(roots[i], 0x21c, 1);
        put_float(roots[i], 0x220, 11000.0f + i * 1000);
        put_u16(roots[i], 0x238, UINT16_MAX);
    }

    resolved_camera_manager_off = -1; resolved_camera_cache_off = -1;
    camera_candidate_count = 0; resolved_mesh_off = -1;
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE, 2);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(self_verified);
    assert(find_entity(item) && !strcmp(find_entity(item)->kind, "loot"));
    assert(!strcmp(find_entity(container)->name, "Loot Container"));
    assert(!strcmp(find_entity(box)->name, "Death Box") && !find_entity(box)->dead);
    assert(find_entity(box)->x == 130.0f && find_entity(decoy) == NULL);
    box_root[0x23a] = 1;
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 3);
    assert(find_entity(box) == NULL);
    box_root[0x23a] = 0;
    put_u16(box_root, 0x238, 0);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 3);
    assert(find_entity(box) == NULL);
    put_u16(box_root, 0x238, UINT16_MAX);
    put_float(item_root, 0x21c, 0);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 3);
    assert(find_entity(item) == NULL);
    put_float(item_root, 0x21c, 1);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    put_ptr(box, OFF_UOBJECT_CLASS, pawn_class);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 3);
    assert(find_entity(box) == NULL);
    put_ptr(box, OFF_UOBJECT_CLASS, blueprint);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);

    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    persistent_sock = sockets[0];
    assert(post_to_radar() == 0);
    char snapshot[8192] = {0};
    assert(recv(sockets[1], snapshot, sizeof(snapshot) - 1, 0) > 0);
    char *death_box = strstr(snapshot, "\"name\":\"Death Box\"");
    assert(strstr(snapshot, "\"kind\":\"loot\"") && death_box);
    char *dead_flag = strstr(death_box, "\"dead\":false");
    char *entity_end = strchr(death_box, '}');
    assert(dead_flag && entity_end && dead_flag < entity_end);
    assert(strstr(snapshot, "\"fov\":105.0"));
    close_radar_sock(); close(sockets[1]);
}

int main(void) {
    game_pid = getpid();
    use_vm_readv = 1;
    test_character_identity();
    test_aim_metadata();
    test_sdk_world_admission();
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
    _Alignas(8) unsigned char native_pawn_class[0x80] = {0};
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
    put_i32(pawn_class, 0x24, 0xD7);
    put_ptr(pawn_class, 0x40, native_pawn_class);
    put_i32(native_pawn_class, 0x24, 0xD5);
    put_ptr(pawn, OFF_APAWN_CONTROLLER, controller);
    put_ptr(pawn, OFF_AACTOR_ROOT_COMPONENT, root);
    put_ptr(pawn, OFF_APAWN_PLAYER_STATE, player_state);
    put_ptr(player_state, OFF_PLAYER_STATE_NAME, name);
    put_i32(player_state, OFF_PLAYER_STATE_NAME + 8, 5);
    put_i32(player_state, OFF_PLAYER_STATE_NAME + 12, 5);
    put_i32(player_state, OFF_GP_PLAYER_STATE_TEAM_ID, 4);
    put_ptr(player_state, OFF_PLAYER_STATE_PAWN_PRIVATE, pawn);
    assert(pawn_is_controlled((uintptr_t)pawn, (uintptr_t)controller));
    pawn[OFF_CHARACTER_LAST_IS_DEAD] = 1;
    assert(!pawn_is_controlled((uintptr_t)pawn, (uintptr_t)controller));
    pawn[OFF_CHARACTER_LAST_IS_DEAD] = 0;
    assert(!pawn_is_controlled((uintptr_t)pawn, (uintptr_t)camera));
    put_ptr(enemy, OFF_UOBJECT_CLASS, enemy_class);
    put_ptr(enemy, OFF_AACTOR_ROOT_COMPONENT, enemy_root);
    put_ptr(enemy, OFF_APAWN_PLAYER_STATE, enemy_state);
    put_ptr(enemy_state, OFF_PLAYER_STATE_PAWN_PRIVATE, enemy);
    put_ptr(enemy_state, OFF_PLAYER_STATE_NAME, enemy_name);
    put_i32(enemy_state, OFF_PLAYER_STATE_NAME + 8, 6);
    put_i32(enemy_state, OFF_PLAYER_STATE_NAME + 12, 6);
    put_i32(enemy_state, OFF_GP_PLAYER_STATE_TEAM_ID, 7);
    put_float(enemy_root, 0x148, 10500.0f);
    put_u16(enemy_root, 0x154, UINT16_MAX);
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
    put_u16(bot_root, 0x154, UINT16_MAX);
    put_ptr(teammate, OFF_UOBJECT_CLASS, pawn_class);
    put_ptr(teammate, OFF_AACTOR_ROOT_COMPONENT, teammate_root);
    put_ptr(teammate, OFF_APAWN_PLAYER_STATE, teammate_state);
    put_ptr(teammate_state, OFF_PLAYER_STATE_PAWN_PRIVATE, teammate);
    put_ptr(teammate_state, OFF_PLAYER_STATE_NAME, teammate_name);
    put_i32(teammate_state, OFF_PLAYER_STATE_NAME + 8, 5);
    put_i32(teammate_state, OFF_PLAYER_STATE_NAME + 12, 5);
    put_i32(teammate_state, OFF_GP_PLAYER_STATE_TEAM_ID, 4);
    put_float(teammate_root, 0x148, 11500.0f);
    put_u16(teammate_root, 0x154, UINT16_MAX);
    put_ptr(loot, OFF_UOBJECT_CLASS, loot_class);
    put_ptr(loot, OFF_AACTOR_ROOT_COMPONENT, loot_root);
    put_float(loot_root, 0x148, 12000.0f);
    put_float(loot_root, OFF_CAPSULE_COMPONENT_HALF_HEIGHT, 100.0f);
    put_float(root, 0x148, 10000.0f);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE, 1.0f);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE + OFF_CAMERA_CACHE_POV, 10000.0f);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE + OFF_CAMERA_CACHE_POV + OFF_POV_FOV, 105.0f);
    put_float(camera, OFF_CAMERA_CACHE_PRIVATE + OFF_CAMERA_CACHE_POV + 0x20, 50.0f);
    put_float(camera, 0x4c00, 37.0f);
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
    assert(find_entity(pawn)->fov == 105.0f);
    assert(strcmp(entities[0].name, "Test") == 0);
    Entity *remote = find_entity(enemy);
    Entity *ai = find_entity(bot);
    Entity *ally = find_entity(teammate);
    assert(remote && strcmp(remote->kind, "player") == 0 && remote->team == 2);
    assert(strcmp(remote->name, "Enemy") == 0 && remote->native_team == 7);
    assert(ai && strcmp(ai->kind, "ai") == 0 && ai->team == 2);
    assert(ally && strcmp(ally->kind, "player") == 0 && ally->team == 1);
    assert(find_entity(loot) == NULL);
    /* A camera vector with an active handler must not authorize input even
     * when its flag byte is clear. The unmanaged sentinel is admissible. */
    assert(!camera_position_plaintext);
    put_u16(camera, OFF_CAMERA_CACHE_PRIVATE + OFF_CAMERA_CACHE_POV + 0x0C, UINT16_MAX);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(camera_position_plaintext);
    int sockets[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    persistent_sock = sockets[0];
    assert(post_to_radar() == 0);
    char snapshot[8192] = {0};
    ssize_t received = recv(sockets[1], snapshot, sizeof(snapshot) - 1, 0);
    assert(received > 0 && snapshot[received - 1] == '\n');
    assert(strstr(snapshot, "\"localPlayerControlled\":true"));
    assert(strstr(snapshot, "\"diagnostics\":{\"status\":\"world sampled\""));
    char expected_address[80];
    snprintf(expected_address, sizeof(expected_address), "\"actorArray\":\"0x%llx\"",
             (unsigned long long)(uintptr_t)actors);
    assert(strstr(snapshot, expected_address));
    snprintf(expected_address, sizeof(expected_address), "\"classAddress\":\"0x%llx\"",
             (unsigned long long)(uintptr_t)enemy_class);
    assert(strstr(snapshot, expected_address));
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

    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    persistent_sock = sockets[0];
    diagnostic_read_failures = 0;
    unsigned char invalid_byte;
    assert(read_mem(1, &invalid_byte, 1) != 1);
    assert(diagnostic_read_failures == 1 && diagnostic_failure_address == 1);
    assert(post_diagnostics() == 0);
    memset(snapshot, 0, sizeof(snapshot));
    received = recv(sockets[1], snapshot, sizeof(snapshot) - 1, 0);
    assert(received > 0 && snapshot[received - 1] == '\n');
    assert(strstr(snapshot, "\"readFailures\":1"));
    assert(strstr(snapshot, "\"failureAddress\":\"0x1\""));
    assert(!strstr(snapshot, "\"entities\""));
    diagnostics_game_unavailable();
    assert(strcmp(diagnostic_status, "game process unavailable") == 0);
    assert(diagnostic_gworld == 0 && diagnostic_world == 0 && diagnostic_actor_data == 0);
    assert(diagnostic_read_failures == 0);
    close_radar_sock();
    close(sockets[1]);

    /* A cached character address reused for another class is rejected in the
     * current frame, before discovery refreshes its actor cache. */
    put_ptr(enemy, OFF_UOBJECT_CLASS, loot_class);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 3);
    assert(find_entity(enemy) == NULL);
    put_ptr(enemy, OFF_UOBJECT_CLASS, enemy_class);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);

    /* Match master visibility even when bounds or relative location are encrypted. */
    put_float(enemy_root, 0x148 + 8, -1000.0f);
    enemy_root[0x176] = 1;
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(find_entity(enemy) && find_entity(bot) && find_entity(teammate));
    put_u16(enemy_root, 0x154, 12);
    enemy_root[0x156] = 1;
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(find_entity(enemy) && find_entity(bot) && find_entity(teammate));
    assert(!find_entity(enemy)->position_plaintext);
    assert(find_entity(enemy)->position_observation.bounds_index == 12);
    assert(find_entity(enemy)->position_observation.bounds_encrypted == 1);
    assert(find_entity(enemy)->position_observation.world_index == -1);
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    persistent_sock = sockets[0];
    assert(post_to_radar() == 0);
    memset(snapshot, 0, sizeof(snapshot));
    received = recv(sockets[1], snapshot, sizeof(snapshot) - 1, 0);
    assert(received > 0 && strstr(snapshot, player_prefix));
    assert(strstr(snapshot, "\"boundsIndex\":12,\"boundsEncrypted\":1"));
    close_radar_sock();
    close(sockets[1]);
    enemy_root[0x156] = 0;
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    put_u16(enemy_root, 0x154, UINT16_MAX);
    enemy_root[0x156] = 1;
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    enemy_root[0x156] = 0;
    put_u16(bot_root, 0x154, 1);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    assert(find_entity(enemy) && find_entity(bot));
    put_u16(bot_root, 0x154, UINT16_MAX);
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 4);
    put_float(enemy_root, 0x148 + 8, 0.0f);
    enemy_root[0x176] = 0;

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
    test_loot_discovery();
    test_large_snapshot();
    free(read_buf);
    puts("legacy reader regression checks passed");
    return 0;
}
