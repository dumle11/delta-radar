/* Owned-memory regression: the enemy source must not replace the loot source. */
#define _GNU_SOURCE
#define DELTA_WITH_DECODER
#define __ANDROID_API__ 31
#define main scanner_program_main
#include "../memscan.c"
#undef main
#include <assert.h>

static int bounds_calls, transform_calls, decoder_success = 1;
static uint64_t expected_root;
int delta_decode_position(int pid, uint64_t base, uint64_t root, float out[3]) {
    assert(pid == getpid() && base == ue4_base && root == expected_root);
    transform_calls++;
    out[0] = 700; out[1] = 800; out[2] = 900;
    return decoder_success;
}
int delta_decode_character_bounds(int pid, uint64_t base, uint64_t root, float out[3]) {
    assert(pid == getpid() && base == ue4_base && root == expected_root);
    bounds_calls++;
    out[0] = 400; out[1] = 500; out[2] = 600;
    return decoder_success;
}
int delta_decode_character_bounds_sampled(int pid, uint64_t base, uint64_t root, float out[3], long long *sampled_at) {
    if (sampled_at) *sampled_at = sample_time_ms();
    return delta_decode_character_bounds(pid,base,root,out);
}
static void put_float(unsigned char *root, size_t offset, float value) {
    memcpy(root + offset, &value, sizeof(value));
}
int main(void) {
    uint64_t owned[128] = {0};
    unsigned char *root = (unsigned char *)owned;
    expected_root = (uintptr_t)root;
    game_pid = getpid(); use_vm_readv = 1; running = 1;
    ue4_base = UINT64_C(0x7700000000); ue4_end = ue4_base + 0x100000;
    uint16_t plain = UINT16_MAX, encrypted = 7;
    memcpy(root + 0x154, &plain, sizeof(plain));
    put_float(root, 0x148, 100); put_float(root, 0x14c, 200); put_float(root, 0x150, 300);
    put_float(root, 0x21c, 1);
    put_float(root, 0x220, 1000); put_float(root, 0x224, 2000); put_float(root, 0x228, 3000);
    float p[3];
    assert(read_character_position_sampled(expected_root, p, NULL) && p[0] == 100 && p[2] == 300);
    assert(read_component_position(expected_root, OFF_USCENE_WORLD_LOCATION, p) && p[0] == 1000 && p[2] == 3000);
    assert(bounds_calls == 0 && transform_calls == 0);

    /* An encrypted enemy must never expose its plausible-looking raw decoy. */
    memcpy(root + 0x154, &encrypted, sizeof(encrypted));
    assert(read_character_position_sampled(expected_root, p, NULL) && p[0] == 400 && p[2] == 600);
    assert(bounds_calls == 1 && transform_calls == 0);
    assert(read_component_position(expected_root, OFF_USCENE_WORLD_LOCATION, p) && p[0] == 1000);
    root[0x23a] = 1;
    assert(read_component_position(expected_root, OFF_USCENE_WORLD_LOCATION, p) && p[0] == 700);
    assert(bounds_calls == 1 && transform_calls == 1);
    decoder_success = 0;
    assert(!read_character_position_sampled(expected_root, p, NULL));
    assert(!read_component_position(expected_root, OFF_USCENE_WORLD_LOCATION, p));

    memcpy(root + 0x154, &plain, sizeof(plain));
    put_float(root, 0x148, NAN); assert(!read_character_position_sampled(expected_root, p, NULL));
    put_float(root, 0x148, 0); put_float(root, 0x14c, 0); put_float(root, 0x150, 0);
    assert(!read_character_position_sampled(expected_root, p, NULL));
    assert(!read_character_position_sampled(0, p, NULL));
    assert(is_game_heap_ptr(UINT64_C(0x7d25267090)));
    assert(is_game_heap_ptr(UINT64_C(0x7100001000)));
    assert(!is_game_heap_ptr(ue4_base + 0x100));
    assert(!is_game_heap_ptr(UINT64_C(0x7d25267091)));
    puts("Enemy/loot source isolation, encrypted dispatch and ASLR checks passed");
    return 0;
}
