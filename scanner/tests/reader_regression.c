/* Exercises the reader against owned memory, without attaching to a game. */
#define main scanner_main
#include "../memscan.c"
#undef main
#include <assert.h>

int main(void) {
    game_pid = getpid();
    use_vm_readv = 1;
    ue4_base = 0x7e00000000ULL;
    ue4_end = ue4_base + 0x100000;
    assert(is_game_heap_ptr(0x7d25267090ULL));
    assert(is_game_heap_ptr(0x7100001000ULL));
    assert(!is_game_heap_ptr(ue4_base + 0x100));
    assert(!is_game_heap_ptr(0));
    assert(!is_game_heap_ptr(0x7d25267091ULL));

    uint8_t world[0x1000] = {0};
    uint8_t level[0x200] = {0};
    uint8_t state[0x600] = {0};
    uint8_t game_state[0x500] = {0};
    uint64_t actors[1] = {(uintptr_t)state};
    uint64_t states[1] = {(uintptr_t)state};
    uint64_t world_ptr = (uintptr_t)world;
    *(uint64_t *)(world + OFF_UWORLD_PERSISTENT_LEVEL) = (uintptr_t)level;
    *(uint64_t *)(world + OFF_UWORLD_GAME_STATE) = (uintptr_t)game_state;
    *(uint64_t *)(level + 0x98) = (uintptr_t)actors;
    *(int32_t *)(level + 0xA0) = 1;
    *(uint64_t *)(game_state + 0x388) = (uintptr_t)states;
    *(int32_t *)(game_state + 0x390) = 1;
    assert(!world_has_named_player_state(world_ptr));
    _Alignas(8) uint16_t name[] = {'T','e','s','t',0};
    *(uint64_t *)(state + OFF_PLAYER_STATE_NAME) = (uintptr_t)name;
    *(int32_t *)(state + OFF_PLAYER_STATE_NAME + 8) = 5;
    *(int32_t *)(state + OFF_PLAYER_STATE_NAME + 12) = 5;
    assert(world_has_named_player_state(world_ptr));
    *(int32_t *)(game_state + 0x390) = 0;
    num_entities = 3;
    self_verified = 1;
    strcpy(self_entity_id, "old-match");
    assert(read_entities_sdk((uintptr_t)&world_ptr, 1, 0x180, 0x148, 0) == 0);
    assert(num_entities == 0 && self_verified == 0 && self_entity_id[0] == 0);
    assert(read_ptr(0) == 0);
    puts("reader regression checks passed");
    return 0;
}
