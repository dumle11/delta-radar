#define _GNU_SOURCE
#define __ANDROID_API__ 31
#define main scanner_program_main
#include "../memscan.c"
#undef main
#include <assert.h>

int main(void) {
    ue4_base = 0x7700000000ULL; ue4_end = ue4_base + 0x100000;
    const uint64_t old = 0x7d00001000ULL, fresh = old + 8, extra = old + 16;
    const uint64_t loot = old + 24, box = old + 32;
    uint64_t actors[4] = {old, loot, box, 0};
    uint8_t kinds[4] = {0, 1, 4, 0}, auth[4] = {0};
    uint64_t roster[] = {old, fresh, fresh, 0, ue4_base, old + 1};
    int count = update_cached_combatants(actors, kinds, auth, 3, 4, roster, 6);
    assert(count == 4 && actors[3] == fresh && kinds[3] == 0 && auth[3]);
    assert(actors[1] == loot && kinds[1] == 1 && actors[2] == box && kinds[2] == 4);
    assert(auth[0]);
    // A full cache gives a new character one loot slot, without dropping peers.
    count = update_cached_combatants(actors, kinds, auth, count, 4, &extra, 1);
    assert(count == 4 && actors[2] == extra && kinds[2] == 0 && auth[2]);
    assert(actors[0] == old && actors[1] == loot && actors[3] == fresh);
    // Roster evidence corrects an old cached classification in place.
    count = update_cached_combatants(actors, kinds, auth, count, 4, &loot, 1);
    assert(count == 4 && actors[1] == loot && kinds[1] == 0 && auth[1]);
    uint64_t overflow = old + 40;
    assert(update_cached_combatants(actors, kinds, auth, count, 4, &overflow, 1) == 4);
    assert(actors[0] == old && actors[1] == loot && actors[2] == extra && actors[3] == fresh);
    puts("Live roster additions, duplicate filtering, loot preservation and capacity checks passed");
}
