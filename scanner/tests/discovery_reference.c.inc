/* Frozen validation/search oracle from the verified dfbe410b build.
 * Used only by differential tests; never linked into the Android scanner. */
static int reference_try_validate_uworld(uint64_t uworld_ptr, GWorldCandidate *out) {
    if (is_in_ue4(uworld_ptr)) return 0;

    uint64_t level = read_ptr(uworld_ptr + OFF_UWORLD_PERSISTENT_LEVEL);
    if (!is_heap_ptr(level) || is_in_ue4(level)) return 0;

    /* Sanity: check ULevel->OwningWorld (0xA8) points back roughly to uworld_ptr */
    uint64_t owning_world = read_ptr(level + 0xA8);
    int has_backref = (owning_world == uworld_ptr);

    int best_score = 0;

    /* Path 1: ActorCluster indirect */
    uint64_t cluster = read_ptr(level + OFF_ULEVEL_ACTOR_CLUSTER);
    if (is_heap_ptr(cluster) && !is_in_ue4(cluster)) {
        uint64_t actor_data = read_ptr(cluster + OFF_ACTOR_CONTAINER_ACTORS);
        int32_t count = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 8);
        int32_t max = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 12);

        if (count >= 5 && count <= 50000 && max >= count && max <= 100000 && is_heap_ptr(actor_data)) {
            int check = count < 20 ? count : 20;
            uint64_t ptrs[20];
            if (read_mem(actor_data, ptrs, check * 8) == check * 8) {
                int valid = 0;
                for (int k = 0; k < check; k++)
                    if (is_heap_ptr(ptrs[k])) valid++;
                int score = valid * 100 / check;
                if (has_backref) score += 10;
                if (score > best_score && valid >= 3) {
                    best_score = score;
                    out->level = level;
                    out->actor_data = actor_data;
                    out->actor_count = count;
                    out->level_offset = OFF_UWORLD_PERSISTENT_LEVEL;
                    out->actor_source = 0;
                    out->score = score;
                }
            }
        }
    }

    /* Path 2: direct TArray in ULevel pad area */
    for (int i = 0; i < (int)NUM_ULEVEL_ACTOR_OFFSETS; i++) {
        int off = ULEVEL_ACTOR_OFFSETS[i];
        uint64_t actor_data = read_ptr(level + off);
        int32_t count = read_i32(level + off + 8);
        int32_t max = read_i32(level + off + 12);

        if (count < 5 || count > 50000 || max < count || max > 100000) continue;
        if (!is_heap_ptr(actor_data)) continue;

        int check = count < 20 ? count : 20;
        uint64_t ptrs[20];
        if (read_mem(actor_data, ptrs, check * 8) != check * 8) continue;

        int valid = 0;
        for (int k = 0; k < check; k++)
            if (is_heap_ptr(ptrs[k])) valid++;
        int score = valid * 100 / check;
        if (has_backref) score += 10;
        if (score > best_score && valid >= 3) {
            best_score = score;
            out->level = level;
            out->actor_data = actor_data;
            out->actor_count = count;
            out->level_offset = OFF_UWORLD_PERSISTENT_LEVEL;
            out->actor_source = 1 + i;
            out->score = score;
        }
    }

    if (best_score > 0) {
        uint64_t game_instance = read_ptr(uworld_ptr + 0x190);
        uint64_t local_players = is_any_ptr(game_instance) ? read_ptr(game_instance + 0x38) : 0;
        uint64_t local_player = is_any_ptr(local_players) ? read_ptr(local_players) : 0;
        uint64_t player_controller = is_any_ptr(local_player) ? read_ptr(local_player + 0x30) : 0;
        uint64_t pawn = is_any_ptr(player_controller) ?
            read_ptr(player_controller + OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN) : 0;
        uint64_t local_class = is_any_ptr(pawn) ? read_ptr(pawn + OFF_UOBJECT_CLASS) : 0;

        /* Actor-pointer validity alone also scores preview/menu worlds highly.
         * Sample the selected list for the concrete pawn layout used by the
         * entity reader so an active gameplay world wins deterministically. */
        int sample_count = out->actor_count < 256 ? out->actor_count : 256;
        uint64_t sample_ptrs[256];
        int plausible_players = 0;
        if (sample_count > 0 &&
            read_mem(out->actor_data, sample_ptrs, (size_t)sample_count * 8) ==
                (ssize_t)((size_t)sample_count * 8)) {
            for (int i = 0; i < sample_count; i++) {
                uint64_t actor = sample_ptrs[i];
                if (!is_any_ptr(actor)) continue;
                uint64_t cls = read_ptr(actor + OFF_UOBJECT_CLASS);
                uint64_t root = read_ptr(actor + OFF_AACTOR_ROOT_COMPONENT);
                if ((!is_any_ptr(local_class) ||
                     (actor != pawn && !is_character_class(cls, local_class))) || !is_any_ptr(root)) continue;
                float x = read_float(root + OFF_USCENE_RELATIVE_LOCATION);
                float y = read_float(root + OFF_USCENE_RELATIVE_LOCATION + 4);
                float z = read_float(root + OFF_USCENE_RELATIVE_LOCATION + 8);
                if (x != x || y != y || z != z ||
                    fabsf(x) > 10000000 || fabsf(y) > 10000000 || fabsf(z) > 10000000 ||
                    (x == 0 && y == 0 && z == 0)) continue;
                plausible_players++;
            }
        }
        out->plausible_players = plausible_players;
        if (plausible_players > 0) {
            int player_bonus = plausible_players * 20;
            best_score += player_bonus < 60 ? player_bonus : 60;
        }

        /* Prefer the active gameplay world over streaming/preview UWorlds. */
        if (is_game_heap_ptr(player_controller)) best_score += 30;
        if (is_game_heap_ptr(pawn)) best_score += 30;
        out->score = best_score;
    }
    return best_score;
}

static void reference_scan_for_gworld(void) {
    fprintf(stderr, "[*] Scanning for GWorld (SDK dump offsets: PersistentLevel=0x%x)...\n",
            OFF_UWORLD_PERSISTENT_LEVEL);
    fprintf(stderr, "[*] UE4 base=0x%lx end=0x%lx (%lu MB)\n",
            (unsigned long)ue4_base, (unsigned long)ue4_end,
            (unsigned long)((ue4_end - ue4_base) / (1024*1024)));

    num_candidates = 0;

    /* First try GWorld at known offset from libUE4.so base (if we had it).
     * The SDK dump doesn't include GWorld offset (World=0x0), so scan. */

    for (int i = 0; i < num_regions && num_candidates < 64; i++) {
        if (regions[i].perms[0] != 'r' || regions[i].perms[1] != 'w') continue;
        if (regions[i].perms[2] == 'x') continue;
        /* Scan libUE4.so rw sections and anonymous regions near UE4 */
        if (!strstr(regions[i].name, "libUE4.so") && regions[i].name[0] != '\0') continue;
        if (regions[i].name[0] == '\0' &&
            (regions[i].start < ue4_base || regions[i].start > ue4_end + 0x10000000)) continue;

        uint64_t size = regions[i].end - regions[i].start;
        if (size < 4096 || size > 200 * 1024 * 1024) continue;

        fprintf(stderr, "[*] Scanning rw 0x%lx (%lu MB) %s\n",
                (unsigned long)regions[i].start, (unsigned long)(size / (1024*1024)),
                regions[i].name[0] ? regions[i].name : "(anon)");

        uint64_t addr = regions[i].start;
        while (addr < regions[i].end && num_candidates < 64) {
            size_t chunk = regions[i].end - addr;
            if (chunk > READ_BUF_SIZE) chunk = READ_BUF_SIZE;

            ssize_t n = read_mem(addr, read_buf, chunk);
            if (n <= 0) { addr += chunk; continue; }

            uint64_t *ptrs = (uint64_t *)read_buf;
            size_t num_ptrs = n / 8;
            for (size_t j = 0; j < num_ptrs && num_candidates < 64; j++) {
                uint64_t val = ptrs[j];
                if (!is_heap_ptr(val)) continue;

                GWorldCandidate cand = {0};
                int score = reference_try_validate_uworld(val, &cand);
                if (score >= 70) {
                    cand.gworld_addr = addr + j * 8;
                    cand.uworld = val;
                    candidates[num_candidates++] = cand;

                    const char *src = cand.actor_source == 0 ?
                        "ActorCluster(0xC8->0x28)" :
                        "DirectULevel";
                    fprintf(stderr, "[+] Candidate #%d: GWorld@0x%lx -> UWorld=0x%lx "
                           "Level=0x%lx actors=%d players=%d score=%d%% src=%s\n",
                           num_candidates, (unsigned long)cand.gworld_addr,
                           (unsigned long)cand.uworld, (unsigned long)cand.level,
                           cand.actor_count, cand.plausible_players, cand.score, src);
                }
            }
            addr += n;
        }
    }
}
