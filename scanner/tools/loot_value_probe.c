/* Test the production price path without starting a scanner or either app. */
#define main scanner_program_main
#include "../memscan.c"
#undef main
int main(int argc, char **argv) {
    alarm(5);
    if (argc < 2 || argc > 66) return 2;
    game_pid = find_game_pid(); use_vm_readv = 1;
    if (game_pid <= 0 || parse_maps() || !ue4_base) return 3;
    loot_values.base = ue4_base; loot_values.read = read_loot_value_memory;
    uint64_t start = diag_now_ms(); lv_refresh(&loot_values, start);
    for (int i = 1; i < argc; i++) {
        char *end = NULL; uint64_t id = strtoull(argv[i], &end, 10); int64_t value = 0;
        if (!end || *end || !id) return 4;
        int known = lv_unit_price(&loot_values, id, &value);
        printf("{\"itemId\":\"%llu\",\"known\":%s,\"unitValue\":%lld}\n",
               (unsigned long long)id, known ? "true" : "false", (long long)value);
    }
    printf("loot-value-probe-complete nodes=%u elapsedMs=%llu\n", loot_values.node_count,
           (unsigned long long)(diag_now_ms() - start));
    return 0;
}
