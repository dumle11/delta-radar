#define _GNU_SOURCE
#define __ANDROID_API__ 31
#define main scanner_program_main
#include "../memscan.c"
#undef main
#include <assert.h>
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

static void set_health(unsigned char *set, float hp, float maximum, float alive) {
    put_float(set, OFF_HEALTH_SET_CURRENT - 4, hp);
    put_float(set, OFF_HEALTH_SET_CURRENT, hp);
    put_float(set, OFF_HEALTH_SET_MAX - 4, maximum);
    put_float(set, OFF_HEALTH_SET_MAX, maximum);
    put_float(set, OFF_HEALTH_SET_IS_ALIVE - 4, alive);
    put_float(set, OFF_HEALTH_SET_IS_ALIVE, alive);
}

static void test_owned_health(void) {
    _Alignas(8) unsigned char actor[0x1800] = {0}, sibling[0x1800] = {0};
    _Alignas(8) unsigned char cls[0x80] = {0}, sibling_cls[0x80] = {0};
    _Alignas(8) unsigned char component[0x500] = {0}, replacement[0x500] = {0};
    _Alignas(8) unsigned char set[0x140] = {0}, other_set[0x140] = {0};
    health_layout_count = 0;
    put_ptr(actor, OFF_UOBJECT_CLASS, cls);
    put_ptr(sibling, OFF_UOBJECT_CLASS, sibling_cls);
    put_ptr(actor, 0x10b8, component);
    put_ptr(component, 0x10, actor);
    put_ptr(component, 0x280, set);
    put_float(component, 0x258, 100); /* design default, not runtime max */
    put_ptr(set, 0x10, actor);
    set_health(set, 95, 95, 1);
    float hp = -1, maximum = -1, alive = -1;
    assert(read_actor_health((uintptr_t)actor, &hp, &maximum, &alive));
    assert(hp == 95 && maximum == 95 && alive == 1);
    set_health(set, 227.5f, 350, 1);
    assert(read_actor_health((uintptr_t)actor, &hp, &maximum, &alive));
    assert(hp == 227.5f && maximum == 350); /* damage, not cached initial HP */
    set_health(set, 0, 350, 0);
    assert(read_actor_health((uintptr_t)actor, &hp, &maximum, &alive));
    assert(hp == 0 && maximum == 350 && alive == 0);
    set_health(set, NAN, 350, 1);
    assert(!read_actor_health((uintptr_t)actor, &hp, &maximum, &alive));
    set_health(set, 200, 350, 1);
    put_ptr(set, 0x10, sibling); /* plausible tuple belonging to another pawn */
    assert(!read_actor_health((uintptr_t)actor, &hp, &maximum, &alive));
    put_ptr(set, 0x10, actor);
    assert(read_actor_health((uintptr_t)actor, &hp, &maximum, &alive));
    put_ptr(component, 0x10, sibling);
    assert(!read_actor_health((uintptr_t)actor, &hp, &maximum, &alive));
    put_ptr(component, 0x10, actor);
    /* Component replacement must use new values at the cached layout. */
    put_ptr(actor, 0x10b8, replacement);
    put_ptr(replacement, 0x10, actor);
    put_ptr(replacement, 0x280, other_set);
    put_float(replacement, 0x258, 100);
    put_ptr(other_set, 0x10, actor);
    set_health(other_set, 74, 95, 1);
    assert(read_actor_health((uintptr_t)actor, &hp, &maximum, &alive));
    assert(hp == 74 && maximum == 95);
    /* An initially uninitialized class must recover on its next valid actor
     * without waiting thousands of frames for another class-wide scan. */
    assert(!read_actor_health((uintptr_t)sibling, &hp, &maximum, &alive));
    put_ptr(sibling, 0x10b8, component);
    put_ptr(component, 0x10, sibling); put_ptr(set, 0x10, sibling);
    set_health(set, 350, 350, 1);
    assert(read_actor_health((uintptr_t)sibling, &hp, &maximum, &alive));
    assert(hp == 350 && maximum == 350);
    health_layout_count = 0;
}


int main(void) {
    game_pid = getpid(); use_vm_readv = 1;
    test_owned_health();
    _Alignas(8) unsigned char actor[0x400] = {0}, state[0x500] = {0};
    put_ptr(state, OFF_PLAYER_STATE_PAWN_PRIVATE, actor);
    assert(actor_is_active_participant((uintptr_t)actor, (uintptr_t)state));
    state[OFF_PLAYER_STATE_FINISHED] = 1;
    assert(!actor_is_active_participant((uintptr_t)actor, (uintptr_t)state));
    state[OFF_PLAYER_STATE_FINISHED] = 0;
    put_ptr(state, OFF_PLAYER_STATE_PAWN_PRIVATE, NULL);
    assert(!actor_is_active_participant((uintptr_t)actor, (uintptr_t)state));
    assert(actor_is_active_participant((uintptr_t)actor, 0));
    _Alignas(8) unsigned char world[0x200]={0}, game_state[0x400]={0};
    uint64_t roster[2]={(uintptr_t)state,(uintptr_t)state};
    put_ptr(world,OFF_UWORLD_GAME_STATE,game_state);
    put_ptr(game_state,0x388,roster);put_i32(game_state,0x390,2);
    put_ptr(state,OFF_PLAYER_STATE_PAWN_PRIVATE,actor);
    put_ptr(actor,OFF_APAWN_PLAYER_STATE,state);
    uint64_t found[2]={0};
    assert(collect_roster_pawns((uintptr_t)world,0,found,2)==1 && found[0]==(uintptr_t)actor);
    state[OFF_PLAYER_STATE_FINISHED]=1;
    assert(collect_roster_pawns((uintptr_t)world,0,found,2)==0);
    assert(collect_roster_pawns((uintptr_t)world,(uintptr_t)actor,found,2)==1);
    put_ptr(actor,OFF_APAWN_PLAYER_STATE,NULL);
    assert(collect_roster_pawns((uintptr_t)world,(uintptr_t)actor,found,2)==0);
    puts("Owned health, active pawn lifecycle and independent roster checks passed");
}
