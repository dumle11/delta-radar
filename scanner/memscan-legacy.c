/*
 * Delta Force memory scanner - finds UE4 structures via process_vm_readv
 * SDK dump offsets from github.com/eikarna/deltaforce-dump (Garena v1.203)
 *
 * Build: aarch64-linux-android26-clang -O2 -lm -o memscan memscan.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <signal.h>
#include <math.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <time.h>

#define GAME_PKG "com.proxima.dfm"
#define RADAR_SOCKET_NAME "delta_radar_feed"
#define MAX_ACTORS 512
#define READ_BUF_SIZE (4 * 1024 * 1024)
#define MAX_REGIONS 16384

/* SDK dump offsets (Garena v1.203, close to Proxima v1.202) */
#define OFF_UWORLD_PERSISTENT_LEVEL   0xF8
#define OFF_UWORLD_GAME_STATE         0x140
#define OFF_UWORLD_LEVELS             0x158
#define OFF_ULEVEL_ACTOR_CLUSTER      0xC8
#define OFF_ACTOR_CONTAINER_ACTORS    0x28
#define OFF_AACTOR_ROOT_COMPONENT     0x180
#define OFF_AACTOR_CHILDREN           0x170
#define OFF_USCENE_RELATIVE_LOCATION  0x164
#define OFF_USCENE_COMPONENT_TO_WORLD 0x1C0
#define OFF_TRANSFORM_TRANSLATION      0x10
#define OFF_USCENE_WORLD_LOCATION      (OFF_USCENE_COMPONENT_TO_WORLD + OFF_TRANSFORM_TRANSLATION)
/* Proxima 1.202 live root transform. Verified against CameraCachePrivate: the
 * local operator root at +0x148 tracks the camera in the same UE world space. */
#define OFF_PROXIMA_ROOT_WORLD_LOCATION 0x148
#define OFF_UOBJECT_CLASS             0x8
#define OFF_UOBJECT_NAME              0x1C
#define OFF_UOBJECT_OUTER             0x10
#define OFF_GNAMES_FROM_UE4_BASE      0x18bacd80
#define OFF_GUOBJECTARRAY_FROM_BASE   0x18bca1a0
#define OFF_FNAME_POOL_BLOCKS         0x38

/* Standard UE4 ULevel::Actors offsets (hidden in pad area) */
static const int ULEVEL_ACTOR_OFFSETS[] = {
    0x98, 0xA0, 0xA8, 0xB0
};
#define NUM_ULEVEL_ACTOR_OFFSETS (sizeof(ULEVEL_ACTOR_OFFSETS)/sizeof(ULEVEL_ACTOR_OFFSETS[0]))

typedef struct {
    uint64_t start, end;
    char perms[8];
    char name[256];
} MemRegion;

static int game_pid = -1;
static int mem_fd = -1;
static uint64_t ue4_base = 0, ue4_end = 0;
static MemRegion regions[MAX_REGIONS];
static int num_regions = 0;
static uint8_t *read_buf = NULL;
static int use_vm_readv = 0;
static FILE *maps_file = NULL;
static unsigned diagnostic_read_failures = 0;
static uint64_t diagnostic_failure_address = 0;
static int diagnostic_failure_errno = 0;

static int find_game_pid(void) {
    DIR *proc = opendir("/proc");
    if (!proc) return -1;
    struct dirent *ent;
    while ((ent = readdir(proc)) != NULL) {
        if (ent->d_type != DT_DIR) continue;
        int pid = atoi(ent->d_name);
        if (pid <= 0) continue;
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/cmdline", pid);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char cmd[256] = {0};
        fread(cmd, 1, sizeof(cmd) - 1, f);
        fclose(f);
        if (strcmp(cmd, GAME_PKG) == 0) { closedir(proc); return pid; }
    }
    closedir(proc);
    return -1;
}

static int parse_maps(void) {
    char path[256];
    snprintf(path, sizeof(path), "/proc/%d/maps", game_pid);
    /* /proc maps is snapshot-like on this kernel: a descriptor opened before
     * libUE4 loads never sees later mappings after rewind. Reopen each time. */
    if (maps_file) { fclose(maps_file); maps_file = NULL; }
    maps_file = fopen(path, "r");
    if (!maps_file) return -1;
    clearerr(maps_file);
    rewind(maps_file);
    FILE *f = maps_file;
    num_regions = 0;
    ue4_base = 0; ue4_end = 0;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        uint64_t start, end;
        char perms[8] = {0}, name[256] = {0};
        if (sscanf(line, "%lx-%lx %4s %*s %*s %*s %255[^\n]", &start, &end, perms, name) < 3)
            continue;
        char *n = name;
        while (*n == ' ') n++;
        if (strstr(n, "libUE4.so")) {
            if (ue4_base == 0 || start < ue4_base) ue4_base = start;
            if (end > ue4_end) ue4_end = end;
        }
        if (num_regions < MAX_REGIONS) {
            regions[num_regions].start = start;
            regions[num_regions].end = end;
            strncpy(regions[num_regions].perms, perms, 7);
            strncpy(regions[num_regions].name, n, 255);
            num_regions++;
        }
    }
    return 0;
}

static int parse_maps_with_retry(void) {
    /* Cold starts after Android's debugger gate can take several seconds to
     * map libUE4 on older devices. Keep the early-authorized proc fd alive. */
    for (int attempt = 0; attempt < 600; attempt++) {
        if (parse_maps() == 0 && num_regions > 0 && ue4_base != 0) return 0;
        usleep(100000);
    }
    return -1;
}

static ssize_t read_mem(uint64_t addr, void *buf, size_t len) {
    ssize_t result;
    if (use_vm_readv) {
        struct iovec local = { .iov_base = buf, .iov_len = len };
        struct iovec remote = { .iov_base = (void *)addr, .iov_len = len };
        result = syscall(__NR_process_vm_readv, game_pid, &local, 1, &remote, 1, 0);
    } else if (mem_fd < 0) {
        errno = EBADF;
        result = -1;
    } else if (lseek64(mem_fd, (off64_t)addr, SEEK_SET) == (off64_t)-1) {
        result = -1;
    } else {
        result = read(mem_fd, buf, len);
    }
    if (result != (ssize_t)len) {
        diagnostic_read_failures++;
        diagnostic_failure_address = addr;
        diagnostic_failure_errno = result < 0 ? errno : 0;
    }
    return result;
}

static uint64_t read_ptr(uint64_t addr) {
    uint64_t val = 0;
    if (read_mem(addr, &val, 8) != 8) return 0;
    return val;
}

static float read_float(uint64_t addr) {
    float val = 0;
    read_mem(addr, &val, 4);
    return val;
}

static int32_t read_i32(uint64_t addr) {
    int32_t val = 0;
    read_mem(addr, &val, 4);
    return val;
}

static int is_heap_ptr(uint64_t p) {
    if (p < 0x10000 || p > 0xFFFFFFFFFFFFULL) return 0;
    if ((p & 0x3) != 0) return 0;
    return 1;
}

static int is_any_ptr(uint64_t p) {
    return p > 0x10000 && p < 0xFFFFFFFFFFFFULL && (p & 0x3) == 0;
}

static int is_in_ue4(uint64_t p) {
    return p >= ue4_base && p < ue4_end;
}

static int is_game_heap_ptr(uint64_t p) {
    return (p & 7) == 0 && p >= 0x6e00000000ULL && p < 0x7800000000ULL;
}

static int is_telemetry_ptr(uint64_t p) {
    return is_any_ptr(p) && (p & 7) == 0 && !is_in_ue4(p);
}

#define OFF_APAWN_CONTROLLER 0x3a8
#define OFF_APAWN_PLAYER_STATE 0x390
#define OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN 0x3F0
#define OFF_PLAYER_STATE_NAME 0x378
#define OFF_PLAYER_STATE_NAME_PRIVATE 0x470
#define OFF_PLAYER_STATE_BOT_FLAGS 0x39e
#define OFF_PLAYER_STATE_PAWN_PRIVATE 0x3F8
#define OFF_GP_PLAYER_STATE_TEAM_ID 0x658
#define OFF_CHARACTER_HEALTH_COMPONENT 0xEC8
#define OFF_HEALTH_COMPONENT_SET 0x248
#define OFF_HEALTH_SET_CURRENT 0x3C
#define OFF_HEALTH_SET_MAX 0x54
#define OFF_HEALTH_SET_IS_ALIVE 0x134
#define OFF_CHARACTER_LAST_IS_DEAD 0xE35
#define OFF_PLAYER_STATE_DEAD 0x4AC
#define OFF_CHARACTER_CAPSULE_HALF_HEIGHT 0xCB0
#define OFF_CAPSULE_COMPONENT_HALF_HEIGHT 0x580

static int read_fstring(uint64_t addr, char *out, int out_size) {
    out[0] = 0;
    if (out_size < 2) return 0;
    uint64_t data = read_ptr(addr);
    int32_t len = read_i32(addr + 8);
    int32_t capacity = read_i32(addr + 12);
    if (!is_any_ptr(data) || len <= 0 || len > 96 || capacity < len || capacity > 512)
        return 0;

    uint16_t chars[97] = {0};
    if (read_mem(data, chars, (size_t)len * 2) != (ssize_t)((size_t)len * 2)) return 0;

    int written = 0;
    for (int i = 0; i < len && written < out_size - 1; i++) {
        uint32_t cp = chars[i];
        if (cp == 0) break;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < len &&
            chars[i + 1] >= 0xDC00 && chars[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (chars[++i] - 0xDC00);
        }
        if (cp < 0x20 || cp == 0x7F) continue;
        if (cp < 0x80 && written + 1 < out_size) {
            out[written++] = (char)cp;
        } else if (cp < 0x800 && written + 2 < out_size) {
            out[written++] = (char)(0xC0 | (cp >> 6));
            out[written++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000 && written + 3 < out_size) {
            out[written++] = (char)(0xE0 | (cp >> 12));
            out[written++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[written++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp <= 0x10FFFF && written + 4 < out_size) {
            out[written++] = (char)(0xF0 | (cp >> 18));
            out[written++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[written++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[written++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[written] = 0;
    return written;
}

static int json_escape(const char *src, char *dst, int dst_size) {
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && n < dst_size - 1; p++) {
        if ((*p == '"' || *p == '\\') && n + 2 < dst_size) {
            dst[n++] = '\\'; dst[n++] = (char)*p;
        } else if (*p == '\n' && n + 2 < dst_size) {
            dst[n++] = '\\'; dst[n++] = 'n';
        } else if (*p == '\r' && n + 2 < dst_size) {
            dst[n++] = '\\'; dst[n++] = 'r';
        } else if (*p >= 0x20) {
            dst[n++] = (char)*p;
        }
    }
    dst[n] = 0;
    return n;
}

/* ========== GWorld scanning using SDK dump offsets ========== */

typedef struct {
    uint64_t gworld_addr;
    uint64_t uworld;
    uint64_t level;
    uint64_t actor_data;
    int actor_count;
    int level_offset;
    int actor_source; // 0=ActorCluster indirect, 1+=direct ULevel offset index
    int plausible_players;
    int score;
} GWorldCandidate;

static GWorldCandidate candidates[64];
static int num_candidates = 0;

/* Preview/lobby UWorlds are structurally valid but have no real PlayerStates.
 * Keep them out of continuous mode so the scanner sleeps instead of repeatedly
 * walking hundreds of megabytes while no match is active. */
static void keep_gameplay_candidates_only(void) {
    int out = 0;
    for (int i = 0; i < num_candidates; i++) {
        if (candidates[i].plausible_players < 1) continue;
        if (out != i) candidates[out] = candidates[i];
        out++;
    }
    num_candidates = out;
}
static int is_character_class(uint64_t cls, uint64_t local_class);

/* FName resolution state (used in scan and run modes) */
static uint64_t fname_pool_addr = 0;
static int fname_blocks_off = OFF_FNAME_POOL_BLOCKS;
static int fname_entry_hdr_size = 2; /* bytes before the ASCII string in FNameEntry */

/* Try to validate a UWorld pointer using SDK dump offsets.
 * Path 1 (SDK dump): UWorld+0xF8 -> ULevel, ULevel+0xC8 -> ActorCluster, ActorCluster+0x28 -> TArray
 * Path 2 (standard UE4): UWorld+0xF8 -> ULevel, ULevel+0x98/0xA0 -> TArray directly (hidden in pad)
 */
static int try_validate_uworld(uint64_t uworld_ptr, GWorldCandidate *out) {
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

/* The matching SDK provides this slot. Do not depend on a pawn happening to
 * occur in the first 256 entries of a heap-scan candidate's actor array. */
static int select_sdk_world(uint64_t slot, GWorldCandidate *out) {
    uint64_t world = read_ptr(slot);
    if (!is_telemetry_ptr(world)) return 0;
    uint64_t level = read_ptr(world + 0xF8);
    uint64_t state = read_ptr(world + 0x140);
    uint64_t instance = read_ptr(world + 0x190);
    if (!is_telemetry_ptr(level) || !is_telemetry_ptr(state) || !is_telemetry_ptr(instance) ||
        read_ptr(level + 0xA8) != world) return 0;
    uint64_t actors = read_ptr(level + 0x98);
    int count = read_i32(level + 0xA0), capacity = read_i32(level + 0xA4);
    uint64_t players = read_ptr(state + 0x388);
    int player_count = read_i32(state + 0x390);
    if (!is_telemetry_ptr(actors) || count < 1 || count > 50000 || capacity < count ||
        capacity > 100000 || !is_telemetry_ptr(players) || player_count < 1 || player_count > 256)
        return 0;
    uint64_t locals = read_ptr(instance + 0x38);
    uint64_t local = is_telemetry_ptr(locals) ? read_ptr(locals) : 0;
    uint64_t controller = is_telemetry_ptr(local) ? read_ptr(local + 0x30) : 0;
    uint64_t camera = is_telemetry_ptr(controller) ? read_ptr(controller + 0x408) : 0;
    if (!is_telemetry_ptr(camera) || read_ptr(camera + 0x370) != controller) return 0;
    int named = 0;
    for (int i = 0; i < player_count && i < 32; i++) {
        uint64_t player = read_ptr(players + (uint64_t)i * 8);
        if (!is_telemetry_ptr(player)) continue;
        char name[64];
        if (read_fstring(player + OFF_PLAYER_STATE_NAME, name, sizeof(name)) > 0 ||
            read_fstring(player + OFF_PLAYER_STATE_NAME_PRIVATE, name, sizeof(name)) > 0) {
            named = 1; break;
        }
    }
    if (!named) return 0;
    memset(out, 0, sizeof(*out));
    out->gworld_addr = slot; out->uworld = world; out->level = level;
    out->actor_data = actors; out->actor_count = count;
    out->actor_source = 1; out->plausible_players = 1; out->score = 160;
    return 1;
}

static void scan_for_gworld(void) {
    GWorldCandidate sdk_world;
    if (select_sdk_world(ue4_base + 0x20AA87A0, &sdk_world)) {
        num_candidates = 1;
        candidates[0] = sdk_world;
        fprintf(stderr, "[+] SDK gameplay world selected: actors=%d\n", sdk_world.actor_count);
        return;
    }
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
                int score = try_validate_uworld(val, &cand);
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

/* ========== Position extraction using SDK offsets ========== */

static void probe_positions(GWorldCandidate *cand) {
    fprintf(stderr, "\n[*] Probing positions (SDK offsets: RootComp=0x%x, RelLoc=0x%x)...\n",
            OFF_AACTOR_ROOT_COMPONENT, OFF_USCENE_RELATIVE_LOCATION);

    int check = cand->actor_count < 50 ? cand->actor_count : 50;
    uint64_t actor_ptrs[50];

    /* Re-read actor data pointer (it might have moved) */
    uint64_t uworld = read_ptr(cand->gworld_addr);
    if (!is_any_ptr(uworld)) { fprintf(stderr, "[-] UWorld invalid\n"); return; }

    uint64_t level = read_ptr(uworld + OFF_UWORLD_PERSISTENT_LEVEL);
    if (!is_any_ptr(level)) { fprintf(stderr, "[-] Level invalid\n"); return; }

    uint64_t actor_data;
    int32_t actor_count;
    if (cand->actor_source == 0) {
        uint64_t cluster = read_ptr(level + OFF_ULEVEL_ACTOR_CLUSTER);
        actor_data = read_ptr(cluster + OFF_ACTOR_CONTAINER_ACTORS);
        actor_count = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 8);
    } else {
        int off = ULEVEL_ACTOR_OFFSETS[cand->actor_source - 1];
        actor_data = read_ptr(level + off);
        actor_count = read_i32(level + off + 8);
    }

    if (actor_count < check) check = actor_count;
    if (check <= 0) { fprintf(stderr, "[-] No actors\n"); return; }

    if (read_mem(actor_data, actor_ptrs, check * 8) != check * 8) {
        fprintf(stderr, "[-] Cannot read actor pointers\n"); return;
    }

    /* Method 1: SDK dump offsets - RootComponent(0x180) -> RelativeLocation(0x164) */
    int valid_sdk = 0, nonzero_sdk = 0;
    fprintf(stderr, "\n--- SDK path: Actor+0x180->RootComp, RootComp+0x164->RelativeLocation ---\n");
    for (int a = 0; a < check; a++) {
        if (!is_any_ptr(actor_ptrs[a])) continue;
        uint64_t root = read_ptr(actor_ptrs[a] + OFF_AACTOR_ROOT_COMPONENT);
        if (!is_any_ptr(root)) continue;

        float x = read_float(root + OFF_USCENE_RELATIVE_LOCATION);
        float y = read_float(root + OFF_USCENE_RELATIVE_LOCATION + 4);
        float z = read_float(root + OFF_USCENE_RELATIVE_LOCATION + 8);

        if (x != x || y != y || z != z) continue;
        if (x < -1000000 || x > 1000000) continue;
        valid_sdk++;
        if (x != 0 || y != 0 || z != 0) nonzero_sdk++;
        if (a < 30) {
            printf("  [SDK] Actor[%d] @0x%lx root=0x%lx: pos=(%.1f, %.1f, %.1f)\n",
                   a, (unsigned long)actor_ptrs[a], (unsigned long)root, x, y, z);
        }
    }
    fprintf(stderr, "[*] SDK offsets: %d valid, %d nonzero out of %d\n",
            valid_sdk, nonzero_sdk, check);

    /* Method 2: ComponentToWorld transform (0x1C0) - contains world-space position
     * The transform matrix has location at offset +0x30 within FTransform (after rotation quat+scale) */
    int valid_ctw = 0, nonzero_ctw = 0;
    fprintf(stderr, "\n--- ComponentToWorld: RootComp+0x1C0+0x30 (world-space FTransform) ---\n");
    for (int a = 0; a < check; a++) {
        if (!is_any_ptr(actor_ptrs[a])) continue;
        uint64_t root = read_ptr(actor_ptrs[a] + OFF_AACTOR_ROOT_COMPONENT);
        if (!is_any_ptr(root)) continue;

        /* FTransform: Rotation(quat 4 doubles=32B) + Translation(3 doubles=24B) at +0x20
         * OR with floats: quat(16B) + vec3(12B) + pad + scale(12B)
         * In UE4 mobile, FTransform uses doubles: rot(32B @ +0x0) + trans(24B @ +0x20) + scale(24B @ +0x38) */
        uint64_t ctw_base = root + OFF_USCENE_COMPONENT_TO_WORLD;

        /* Try doubles first (UE4 default FTransform uses doubles) */
        double dx, dy, dz;
        read_mem(ctw_base + 0x20, &dx, 8);
        read_mem(ctw_base + 0x28, &dy, 8);
        read_mem(ctw_base + 0x30, &dz, 8);

        if (dx != dx || dy != dy || dz != dz) continue;
        if (dx < -1000000 || dx > 1000000) continue;
        valid_ctw++;
        if (dx != 0 || dy != 0 || dz != 0) nonzero_ctw++;
        if (a < 30) {
            printf("  [CTW] Actor[%d] @0x%lx: world=(%.1f, %.1f, %.1f)\n",
                   a, (unsigned long)actor_ptrs[a], dx, dy, dz);
        }
    }
    fprintf(stderr, "[*] ComponentToWorld: %d valid, %d nonzero out of %d\n",
            valid_ctw, nonzero_ctw, check);

    /* Method 3: Brute-force scan nearby offsets if SDK offsets yield all zeros */
    if (nonzero_sdk < 3 && nonzero_ctw < 3) {
        fprintf(stderr, "\n[*] SDK offsets mostly zero, trying brute-force offset scan...\n");
        int best_nonzero = 0, best_root = 0, best_pos = 0, best_valid = 0;

        /* Scan root component offsets near 0x180 and position offsets near 0x164 */
        for (int roff = 0x160; roff <= 0x1A0; roff += 8) {
            for (int poff = 0x100; poff <= 0x300; poff += 4) {
                int valid = 0, nonzero = 0;
                for (int a = 0; a < check; a++) {
                    if (!is_any_ptr(actor_ptrs[a])) continue;
                    uint64_t root = read_ptr(actor_ptrs[a] + roff);
                    if (!is_any_ptr(root)) continue;
                    float x = read_float(root + poff);
                    float y = read_float(root + poff + 4);
                    float z = read_float(root + poff + 8);
                    if (x != x || y != y || z != z) continue;
                    if (x < -1000000 || x > 1000000) continue;
                    valid++;
                    if (x != 0 || y != 0 || z != 0) nonzero++;
                }
                if (nonzero > best_nonzero || (nonzero == best_nonzero && valid > best_valid)) {
                    best_nonzero = nonzero;
                    best_valid = valid;
                    best_root = roff;
                    best_pos = poff;
                }
            }
        }
        if (best_nonzero > 0) {
            fprintf(stderr, "[+] Brute-force best: root=+0x%x pos=+0x%x (%d valid, %d nonzero)\n",
                    best_root, best_pos, best_valid, best_nonzero);
            for (int a = 0; a < check && a < 20; a++) {
                if (!is_any_ptr(actor_ptrs[a])) continue;
                uint64_t root = read_ptr(actor_ptrs[a] + best_root);
                if (!is_any_ptr(root)) continue;
                float x = read_float(root + best_pos);
                float y = read_float(root + best_pos + 4);
                float z = read_float(root + best_pos + 8);
                if (x < -1000000 || x > 1000000) continue;
                if (x != 0 || y != 0 || z != 0) {
                    printf("  [BF] Actor[%d]: pos=(%.1f, %.1f, %.1f)\n", a, x, y, z);
                }
            }
        }
    }

    /* Method 4: Try FName resolution from GNames pool */
    if (ue4_base > 0) {
        uint64_t gnames = ue4_base + OFF_GNAMES_FROM_UE4_BASE;
        fprintf(stderr, "\n[*] Trying GNames at 0x%lx (base+0x%x)\n",
                (unsigned long)gnames, OFF_GNAMES_FROM_UE4_BASE);

        /* Verify FNamePool: read BlocksPtr at +0x38 (BlocksOff from dump) */
        /* FNamePool layout: header, then blocks array at +0x38 */
        /* Each block is a page of FNameEntry records */
        /* Actually, FNamePool has: CurrentBlock/CurrentBytesUsed at start,
         * then Blocks[FNameMaxBlockBits] starting at some offset.
         * The dump says BlocksOff=0x38, Stride=2, BlocksBit=18 */

        /* Probe multiple blocks offsets to find block0 */
        static const int scan_blocks_offsets[] = {0x38, 0x10, 0x18, 0x20, 0x28, 0x30, 0x40, 0x48, 0x50};
        uint64_t block0_ptr = 0;
        int found_boff = -1;
        for (int bi = 0; bi < (int)(sizeof(scan_blocks_offsets)/sizeof(scan_blocks_offsets[0])); bi++) {
            uint64_t b0 = read_ptr(gnames + scan_blocks_offsets[bi]);
            if (!is_any_ptr(b0)) continue;
            char test[8];
            if (read_mem(b0 + 2, test, 4) == 4 && memcmp(test, "None", 4) == 0) {
                block0_ptr = b0;
                found_boff = scan_blocks_offsets[bi];
                fname_pool_addr = gnames;
                fname_blocks_off = found_boff;
                fprintf(stderr, "[+] GNames block0 found at blocks@+0x%x ptr=0x%lx\n",
                        found_boff, (unsigned long)block0_ptr);
                break;
            }
        }
        if (is_any_ptr(block0_ptr)) {
            char namebuf[256];
            if (read_mem(block0_ptr, namebuf, 256) == 256) {
                for (int off = 0; off < 240; off += 2) {
                    uint16_t hdr = *(uint16_t *)(namebuf + off);
                    int len = hdr >> 6;
                    if (len >= 3 && len <= 64 && off + 2 + len <= 256) {
                        int ok = 1;
                        for (int c = 0; c < len; c++)
                            if (namebuf[off+2+c] < 32 || namebuf[off+2+c] > 126) { ok = 0; break; }
                        if (ok) {
                            char tmp[65];
                            memcpy(tmp, namebuf + off + 2, len);
                            tmp[len] = 0;
                            fprintf(stderr, "  GNames @+%d: \"%s\"\n", off, tmp);
                        }
                    }
                }
            }
        } else {
            fprintf(stderr, "[-] GNames block0 not found at any blocks offset\n");
        }
    }

    /* Print actor UObject info (class, name) */
    fprintf(stderr, "\n[*] Actor UObject info (first 10):\n");
    for (int a = 0; a < check && a < 10; a++) {
        if (!is_any_ptr(actor_ptrs[a])) continue;
        uint64_t cls = read_ptr(actor_ptrs[a] + OFF_UOBJECT_CLASS);
        uint64_t outer = read_ptr(actor_ptrs[a] + OFF_UOBJECT_OUTER);
        int32_t name_idx = read_i32(actor_ptrs[a] + OFF_UOBJECT_NAME);
        int32_t name_num = read_i32(actor_ptrs[a] + OFF_UOBJECT_NAME + 4);

        /* Read class name index */
        int32_t cls_name_idx = 0;
        if (is_any_ptr(cls))
            cls_name_idx = read_i32(cls + OFF_UOBJECT_NAME);

        fprintf(stderr, "  Actor[%d] @0x%lx: class=0x%lx(name_idx=%d) name_idx=%d_%d outer=0x%lx\n",
                a, (unsigned long)actor_ptrs[a], (unsigned long)cls, cls_name_idx,
                name_idx, name_num, (unsigned long)outer);
    }
}

/* ========== FName resolution ========== */

/* fname_pool_addr and fname_blocks_off declared earlier (forward decl) */

/* Resolve an FName index to a string.
 * FNamePool has blocks at fname_blocks_off. Block index = idx >> BlocksBit(18).
 * Entry offset = (idx & ((1<<18)-1)) * Stride(2).
 * Each FNameEntry: 2-byte header (length = hdr >> 6), then ASCII string. */
static int resolve_fname(int32_t idx, char *out, int out_size) {
    if (fname_pool_addr == 0 || idx < 0) { out[0] = 0; return 0; }

    int block_idx = idx >> 18;
    int entry_off = (idx & ((1 << 18) - 1)) * 2;

    uint64_t block_ptr = read_ptr(fname_pool_addr + fname_blocks_off + block_idx * 8);
    if (!is_any_ptr(block_ptr)) { out[0] = 0; return 0; }

    int hdr_sz = fname_entry_hdr_size;
    uint8_t hdr_buf[8];
    if (read_mem(block_ptr + entry_off, hdr_buf, hdr_sz) != hdr_sz) { out[0] = 0; return 0; }

    int len = 0;
    if (hdr_sz == 2) {
        uint16_t h = hdr_buf[0] | (hdr_buf[1] << 8);
        len = h >> 6;
    } else if (hdr_sz == 4) {
        /* 4-byte header: try UE5-style (len in bits 1..15 of first 2 bytes) */
        uint16_t h = hdr_buf[0] | (hdr_buf[1] << 8);
        len = h >> 6;
        if (len <= 0 || len >= out_size) {
            /* Try: first 2 bytes = hash, next 2 bytes = length */
            len = hdr_buf[2] | (hdr_buf[3] << 8);
        }
    } else {
        /* 6 or 8 byte header: try hash(2)+len(2) at various positions */
        uint16_t h = hdr_buf[0] | (hdr_buf[1] << 8);
        len = h >> 6;
        if (len <= 0 || len >= out_size)
            len = hdr_buf[2] | (hdr_buf[3] << 8);
        if (len <= 0 || len >= out_size)
            len = hdr_buf[hdr_sz - 2] | (hdr_buf[hdr_sz - 1] << 8);
    }
    if (len <= 0 || len >= out_size) { out[0] = 0; return 0; }

    if (read_mem(block_ptr + entry_off + hdr_sz, out, len) != len) { out[0] = 0; return 0; }
    out[len] = 0;
    return len;
}

/* ========== RUNNING MODE: Read actors and POST to radar ========== */

static int find_object_field(uint64_t owner, int start, int end, const char *class_fragment);

#include "skeleton-math.h"
/* Bone indices belong to each mesh's reference skeleton. The packet carries
 * that asset's parent indices rather than a guessed humanoid mapping. */

typedef struct {
    uint64_t root;
    float bounds[3];
    int present, decoded;
    int bounds_index, bounds_encrypted, bounds_flags;
    int world_index, world_encrypted, world_flags;
} PositionObservation;

typedef struct {
    char id[32];
    float x, y, z, yaw, pitch, fov;
    char name[64];
    char kind[16];
    char class_name[64];
    uint64_t class_address;
    float health, max_health, half_height;
    int health_known;
    int position_plaintext;
    PositionObservation position_observation;
    int rendered_on_screen;
    int dead;
    int team;
    int native_team;
    float bone_pos[MAX_SKELETON_BONES][3]; /* world-space bone positions in meters */
    int has_bones, bone_count;
    int16_t bone_parent[MAX_SKELETON_BONES];
} Entity;

static Entity entities[MAX_ACTORS];
static int num_entities = 0;
static uint64_t diagnostic_gworld = 0, diagnostic_world = 0, diagnostic_level = 0;
static uint64_t diagnostic_actor_data = 0;
static int diagnostic_actor_count = 0;
static const char *diagnostic_status = "waiting for gameplay";

static void diagnostics_game_unavailable(void) {
    diagnostic_status = "game process unavailable";
    diagnostic_gworld = diagnostic_world = diagnostic_level = diagnostic_actor_data = 0;
    diagnostic_actor_count = 0;
    diagnostic_read_failures = 0;
    diagnostic_failure_address = 0;
    diagnostic_failure_errno = 0;
}

/* RelativeRotation: FRotator at root+0x178 = {Pitch, Yaw, Roll} as floats (degrees) */
#define OFF_USCENE_RELATIVE_PITCH 0x178
#define OFF_USCENE_RELATIVE_YAW   0x17c

/* APlayerController::ControlRotation (camera rotation, degrees, can exceed 360) */
#define OFF_PC_CONTROL_PITCH 0x3D8
#define OFF_PC_CONTROL_YAW   0x3DC
#define OFF_PC_CAMERA_MANAGER 0x408
#define OFF_CAMERA_CACHE_PRIVATE 0x2D60
#define OFF_CAMERA_CACHE_POV 0x10
#define OFF_POV_LOCATION 0x0
#define OFF_POV_ROTATION 0x10
#define OFF_POV_FOV 0x1C

static char self_entity_id[32] = {0};
/* Loading/lobby worlds can contain pawn-shaped preview actors. Only publish
 * after the local pawn resolves to a real, named PlayerState. */
static int self_verified = 0;
static uint64_t verified_uworld = 0;
static int match_was_verified = 0;
static float last_camera_timestamp = -1.0f;
static int camera_position_plaintext = 0;
static int local_player_controlled = 0;
static long long camera_sampled_at_ms = 0;
static unsigned stale_camera_frames = 0;
static int resolved_camera_manager_off = -1;
static int resolved_camera_cache_off = -1;
static int camera_candidate_offsets[128];
static float camera_candidate_timestamps[128];
static int camera_candidate_count = 0;

/* Do not compare with generic ancestors of the local pawn: lobby/spectator
 * pawns can reach Actor/Object within five levels and admit the whole world.
 * Require the matching SDK's native Character -> Pawn registration pair.
 * Blueprint indices may vary, so only this native anchor is compared. */
static int is_character_class(uint64_t cls, uint64_t local_class) {
    (void)local_class;
    for (int depth = 0; depth < 24 && is_any_ptr(cls); depth++) {
        int32_t index = -1, parent_index = -1;
        uint64_t next = 0;
        if (read_mem(cls + 0x24, &index, sizeof(index)) != sizeof(index) ||
            read_mem(cls + 0x40, &next, sizeof(next)) != sizeof(next)) return 0;
        if (index == 0xD7 && is_any_ptr(next) &&
            read_mem(next + 0x24, &parent_index, sizeof(parent_index)) == sizeof(parent_index) &&
            parent_index == 0xD5) return 1;
        if (next == cls) break;
        cls = next;
    }
    return 0;
}

static int pawn_is_controlled(uint64_t pawn, uint64_t pc) {
    if (!is_telemetry_ptr(pawn) || !is_telemetry_ptr(pc) ||
        !is_character_class(read_ptr(pawn + OFF_UOBJECT_CLASS), 0) ||
        read_ptr(pawn + OFF_APAWN_CONTROLLER) != pc) return 0;
    uint64_t state = read_ptr(pawn + OFF_APAWN_PLAYER_STATE);
    uint8_t dead = 1;
    return is_telemetry_ptr(state) &&
        read_ptr(state + OFF_PLAYER_STATE_PAWN_PRIVATE) == pawn &&
        read_mem(pawn + OFF_CHARACTER_LAST_IS_DEAD, &dead, 1) == 1 && dead == 0;
}

enum LootKind {
    LOOT_NONE, LOOT_ITEM, LOOT_CONTAINER, LOOT_STASH, LOOT_DEATH_BOX
};

/* Native registration indices and parent indices from the matching SDK dump.
 * Blueprint subclasses are resolved by walking to their native ancestor. */
static int loot_kind_from_class(uint64_t cls) {
    static const struct { int32_t index, parent; enum LootKind kind; } anchors[] = {
        {6322, 57, LOOT_STASH}, {7559, 7557, LOOT_STASH},
        {7823, 6851, LOOT_ITEM}, {7824, 7823, LOOT_ITEM},
        {7825, 7824, LOOT_CONTAINER}, {7826, 7825, LOOT_DEATH_BOX},
        {8067, 57, LOOT_ITEM}
    };
    for (int depth = 0; depth < 16 && is_any_ptr(cls); depth++) {
        int32_t index, parent_index;
        uint64_t parent;
        if (read_mem(cls + 0x24, &index, sizeof(index)) != sizeof(index) ||
            read_mem(cls + 0x40, &parent, sizeof(parent)) != sizeof(parent))
            return -1;
        for (size_t i = 0; i < sizeof(anchors) / sizeof(anchors[0]); i++) {
            if (index != anchors[i].index) continue;
            if (!is_any_ptr(parent) ||
                read_mem(parent + 0x24, &parent_index, sizeof(parent_index)) != sizeof(parent_index))
                return -1;
            if (parent_index == anchors[i].parent) return anchors[i].kind;
        }
        if (parent == cls) break;
        cls = parent;
    }
    return LOOT_NONE;
}

/* Captured 1.202 ARM64 getters dispatch through the decoder when Index is not
 * UINT16_MAX (in encryption mode 2), independently of bEncrypted. Only the
 * unregistered sentinel plus a clear flag is safe without reading that mode. */
static int enc_handler_unmanaged(const uint8_t handler[4]) {
    return handler[0] == 0xFF && handler[1] == 0xFF && handler[2] == 0;
}

#include "position-decoder.h"
#include "skeleton-reader.h"

static int read_plain_world_position(uint64_t root, float out[3], PositionObservation *observation) {
    float transform[12];
    if (read_mem(root + 0x210, transform, sizeof(transform)) != sizeof(transform)) {
        static unsigned errors = 0;
        if (errors++ < 5) fprintf(stderr, "[!] Cannot read component world transform\n");
        return 0;
    }
    const uint8_t *handler = (const uint8_t *)transform + 0x28;
    if (observation) {
        observation->world_index = handler[0] | ((int)handler[1] << 8);
        observation->world_encrypted = handler[2];
        observation->world_flags = handler[3];
    }
    if (!enc_handler_unmanaged(handler)) {
        static unsigned encrypted = 0;
        if ((encrypted++ % 300) == 0)
            fprintf(stderr, "[!] Skipping managed/encrypted component transform without a verified decoder\n");
        return 0;
    }
    float norm = 0;
    for (int i = 0; i < 4; i++) {
        if (!isfinite(transform[i])) return 0;
        norm += transform[i] * transform[i];
    }
    if (norm < 0.5f || norm > 1.5f) return 0;
    for (int i = 0; i < 3; i++) {
        out[i] = transform[i + 4];
        if (!isfinite(out[i]) || fabsf(out[i]) > 10000000) return 0;
    }
    return out[0] != 0 || out[1] != 0 || out[2] != 0;
}

/* A clear bounds tag alone is insufficient: the component's independently
 * tagged world transform must be plaintext and agree within 50 cm. This is a
 * consistency check, not a decoder or line-of-sight test. Other CLI position
 * sources and legacy raw visual coordinates remain unverified. */
static int read_actor_position(uint64_t root, int offset, float out[3], int *plaintext,
                               PositionObservation *observation) {
    uint8_t vector[16];
    *plaintext = 0;
    if (observation) {
        memset(observation, 0, sizeof(*observation));
        observation->root = root;
        observation->bounds_index = observation->bounds_encrypted = observation->bounds_flags = -1;
        observation->world_index = observation->world_encrypted = observation->world_flags = -1;
    }
    ssize_t got = read_mem(root + (uint64_t)offset, vector, sizeof(vector));
    if (got < 12) return 0;
    memcpy(out, vector, 12);
    for (int i = 0; i < 3; i++)
        if (!isfinite(out[i]) || fabsf(out[i]) > 10000000.0f) return 0;
    if (observation) {
        observation->present = 1;
        memcpy(observation->bounds, out, sizeof(observation->bounds));
        if (got == sizeof(vector) && offset == OFF_PROXIMA_ROOT_WORLD_LOCATION) {
            observation->bounds_index = vector[12] | ((int)vector[13] << 8);
            observation->bounds_encrypted = vector[14];
            observation->bounds_flags = vector[15];
        }
    }
    /* Copy coordinates and their encryption tag together. A separate tag
     * read could describe a different engine update than the coordinates. */
    *plaintext = got == sizeof(vector) && offset == OFF_PROXIMA_ROOT_WORLD_LOCATION &&
        enc_handler_unmanaged(vector + 0x0C);
    if (*plaintext) {
        float world[3], distance_squared = 0;
        if (!read_plain_world_position(root, world, observation)) *plaintext = 0;
        else {
            for (int i = 0; i < 3; i++) {
                float delta = world[i] - out[i];
                distance_squared += delta * delta;
            }
            if (distance_squared > 50.0f * 50.0f) *plaintext = 0;
        }
    }
    return 1;
}

static int actor_rendered_on_screen(uint64_t actor) {
    uint64_t mesh = read_ptr(actor + 0x3D0); /* ACharacter::Mesh, matching SDK */
    if (!is_telemetry_ptr(mesh)) return 0;
    uint8_t flags = 0;
    return read_mem(mesh + 0x8B8, &flags, 1) == 1 && (flags & 1) != 0;
}

static uint64_t cached_local_class = 0;
static float cached_self_pos[3] = {0};

/* Resolve version-sensitive UObject pointer fields by the pointed object's
 * reflected class name once per process. This avoids per-frame blind scans
 * while remaining robust across minor mobile builds. */
static int find_object_field(uint64_t owner, int start, int end, const char *class_fragment) {
    if (!is_any_ptr(owner) || !fname_pool_addr) return -1;
    for (int off = start; off < end; off += 8) {
        uint64_t object = read_ptr(owner + (uint64_t)off);
        if (!is_game_heap_ptr(object)) continue;
        uint64_t cls = read_ptr(object + OFF_UOBJECT_CLASS);
        if (!is_any_ptr(cls)) continue;
        char class_name[96] = {0};
        resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), class_name, sizeof(class_name));
        if (class_name[0] && strstr(class_name, class_fragment)) return off;
    }
    return -1;
}

/* FGPGameplayAttributeData stores BaseValue immediately before CurrentValue.
 * Proxima 1.202's live Health/MaxHealth/IsAlive CurrentValue slots are
 * 0x3C/0x54/0x134. Validate the complete tuple so an unrelated UObject is not
 * accepted merely because it happens to contain one plausible float. */
static int read_health_set(uint64_t health_set, float *health, float *max_health,
                           float *is_alive) {
    if (!is_game_heap_ptr(health_set)) return 0;
    float hp_base = read_float(health_set + OFF_HEALTH_SET_CURRENT - 4);
    float hp = read_float(health_set + OFF_HEALTH_SET_CURRENT);
    float max_base = read_float(health_set + OFF_HEALTH_SET_MAX - 4);
    float max_hp = read_float(health_set + OFF_HEALTH_SET_MAX);
    float alive_base = read_float(health_set + OFF_HEALTH_SET_IS_ALIVE - 4);
    float alive = read_float(health_set + OFF_HEALTH_SET_IS_ALIVE);
    if (!isfinite(hp_base) || !isfinite(hp) || !isfinite(max_base) ||
        !isfinite(max_hp) || !isfinite(alive_base) || !isfinite(alive)) return 0;
    if (max_hp < 10.0f || max_hp > 100000.0f ||
        max_base < 10.0f || max_base > 100000.0f) return 0;
    if (hp < 0.0f || hp > max_hp * 1.5f ||
        hp_base < 0.0f || hp_base > max_base * 1.5f) return 0;
    if (alive < -0.01f || alive > 1.01f ||
        alive_base < -0.01f || alive_base > 1.01f) return 0;
    if (alive < 0.5f && hp > max_hp * 0.05f) return 0;
    *health = hp;
    *max_health = max_hp;
    *is_alive = alive;
    return 1;
}

/* Minor regional builds shift the large AGPCharacterBase layout. Resolve the
 * two pointer fields once from the local pawn, then reuse them for every actor
 * and frame. Reflection names are preferred, with a strict value-shape check
 * as the fallback when this build's FName layout cannot decode component names. */
static int discover_health_chain(uint64_t actor, int *component_off, int *set_off) {
    int best_score = -1;
    int best_component_off = -1, best_set_off = -1;
    float best_hp = 0.0f, best_max_hp = 0.0f, best_alive = 0.0f;
    int reflected_component = find_object_field(
        actor, 0x700, 0x1800, "HealthDataComponent");
    int component_start = reflected_component >= 0 ? reflected_component : 0x700;
    int component_end = reflected_component >= 0 ? reflected_component + 8 : 0x1800;
    for (int co = component_start; co < component_end; co += 8) {
        uint64_t component = read_ptr(actor + (uint64_t)co);
        if (!is_game_heap_ptr(component)) continue;

        int reflected_set = find_object_field(
            component, 0x100, 0x500, "AttributeSetHealth");
        int set_start = reflected_set >= 0 ? reflected_set : 0x100;
        int set_end = reflected_set >= 0 ? reflected_set + 8 : 0x500;
        for (int so = set_start; so < set_end; so += 8) {
            uint64_t set = read_ptr(component + (uint64_t)so);
            float hp = 0.0f, max_hp = 0.0f, alive = 0.0f;
            if (!read_health_set(set, &hp, &max_hp, &alive)) continue;
            /* In UGPHealthDataComponent, HealthMAX is 0x28 bytes before the
             * HealthSet pointer. This relationship survives the regional
             * build's base-class size shift and rejects unrelated attribute-
             * shaped objects that happen to contain plausible numbers. */
            if (so < 0x28) continue;
            float component_max = read_float(component + (uint64_t)(so - 0x28));
            if (!isfinite(component_max) || component_max < 10.0f ||
                fabsf(component_max - max_hp) > fmaxf(2.0f, max_hp * 0.05f)) continue;
            int score = 0;
            if (alive >= 0.5f) score += 100;
            if (hp > 0.01f) score += 100;
            if (score > best_score) {
                best_score = score;
                best_component_off = co;
                best_set_off = so;
                best_hp = hp;
                best_max_hp = max_hp;
                best_alive = alive;
            }
        }
    }
    if (best_component_off < 0) return 0;
    *component_off = best_component_off;
    *set_off = best_set_off;
    fprintf(stderr,
            "\n[+] Health chain actor+0x%x -> component+0x%x "
            "(hp=%.1f/%.1f alive=%.0f score=%d)\n",
            best_component_off, best_set_off, best_hp, best_max_hp,
            best_alive, best_score);
    if (best_score == 0) {
        uint64_t component = read_ptr(actor + (uint64_t)best_component_off);
        uint64_t set = read_ptr(component + (uint64_t)best_set_off);
        fprintf(stderr, "[diag] health-set candidate floats:");
        for (int off = 0x20; off <= 0x140; off += 4) {
            float value = read_float(set + (uint64_t)off);
            if (isfinite(value) && fabsf(value) >= 0.5f && fabsf(value) <= 100000.0f)
                fprintf(stderr, " +%03x=%.2f", off, value);
        }
        fprintf(stderr, "\n");
    }
    return 1;
}

#define MAX_HEALTH_LAYOUTS 64
typedef struct {
    uint64_t actor_class;
    int component_off;
    int set_off;
    int death_trusted;
    unsigned long last_attempt;
} HealthLayout;

static HealthLayout health_layouts[MAX_HEALTH_LAYOUTS];
static int health_layout_count = 0;
static unsigned long health_layout_clock = 0;

static HealthLayout *health_layout_for_actor(uint64_t actor) {
    uint64_t actor_class = read_ptr(actor + OFF_UOBJECT_CLASS);
    if (!is_game_heap_ptr(actor_class) && !is_any_ptr(actor_class)) return NULL;
    health_layout_clock++;

    HealthLayout *layout = NULL;
    for (int i = 0; i < health_layout_count; i++) {
        if (health_layouts[i].actor_class == actor_class) {
            layout = &health_layouts[i];
            break;
        }
    }
    if (!layout) {
        if (health_layout_count >= MAX_HEALTH_LAYOUTS) return NULL;
        layout = &health_layouts[health_layout_count++];
        memset(layout, 0, sizeof(*layout));
        layout->actor_class = actor_class;
        layout->component_off = -1;
        layout->set_off = -1;
    }

    /* Retry a failed class occasionally: its first observed actor may still be
     * constructing components. Successful layouts never scan again. */
    if (layout->component_off < 0 &&
        (layout->last_attempt == 0 || health_layout_clock - layout->last_attempt >= 3000)) {
        layout->last_attempt = health_layout_clock;
        int component_off = -1, set_off = -1;
        if (discover_health_chain(actor, &component_off, &set_off)) {
            layout->component_off = component_off;
            layout->set_off = set_off;
            uint64_t component = read_ptr(actor + (uint64_t)component_off);
            uint64_t set = is_game_heap_ptr(component) ?
                read_ptr(component + (uint64_t)set_off) : 0;
            float hp = 0.0f, max_hp = 0.0f, alive = 0.0f;
            layout->death_trusted = read_health_set(set, &hp, &max_hp, &alive) &&
                hp > 0.01f && alive >= 0.5f;
        }
    }
    return layout;
}

/* FName layouts differ in the current Proxima build, so reflection can fail
 * even though the object layout is intact.  APlayerCameraManager has a strong
 * structural signature: PCOwner points back to the controller and DefaultFOV
 * is a sane angle.  Resolve it once without accepting unrelated UObject
 * pointers at the old fixed offset. */
static int find_camera_manager_field(uint64_t player_controller) {
    for (int off = 0x300; off < 0x900; off += 8) {
        uint64_t candidate = read_ptr(player_controller + (uint64_t)off);
        if (!is_telemetry_ptr(candidate)) continue;
        if (read_ptr(candidate + 0x370) != player_controller) continue;
        float default_fov = read_float(candidate + 0x388);
        if (default_fov == default_fov && default_fov >= 20.0f && default_fov <= 180.0f)
            return off;
    }
    return -1;
}

static void discover_camera_caches(uint64_t camera_manager, const float pawn_pos[3]) {
    /* Locate FMinimalViewInfo by its contiguous Location, Rotation and FOV.
     * This runs once after resolving PlayerCameraManager and survives layout
     * shifts between minor game versions. */
    camera_candidate_count = 0;

    /* Seed the exact cache from the matching SDK first.  Dynamic discovery is
     * retained below for minor-version shifts, but the camera can legitimately
     * be more than ten metres from the acknowledged pawn during deployment or
     * spectating, which made the proximity-only search reject the real cache. */
    {
        int cache_off = OFF_CAMERA_CACHE_PRIVATE;
        int fov_off = cache_off + OFF_CAMERA_CACHE_POV + OFF_POV_FOV;
        float fov = read_float(camera_manager + (uint64_t)fov_off);
        float timestamp = read_float(camera_manager + (uint64_t)cache_off);
        if (fov == fov && fov >= 20.0f && fov <= 180.0f && timestamp == timestamp) {
            camera_candidate_offsets[camera_candidate_count] = cache_off;
            camera_candidate_timestamps[camera_candidate_count] = timestamp;
            camera_candidate_count++;
        }
    }
    for (int fov_off = 0x300; fov_off < 0x5000 && camera_candidate_count < 128; fov_off += 4) {
        float fov = read_float(camera_manager + (uint64_t)fov_off);
        if (!(fov >= 20.0f && fov <= 180.0f)) continue;
        int location_off = fov_off - OFF_POV_FOV;
        float x = read_float(camera_manager + (uint64_t)location_off);
        float y = read_float(camera_manager + (uint64_t)location_off + 4);
        float z = read_float(camera_manager + (uint64_t)location_off + 8);
        float pitch = read_float(camera_manager + (uint64_t)location_off + OFF_POV_ROTATION);
        float yaw = read_float(camera_manager + (uint64_t)location_off + OFF_POV_ROTATION + 4);
        if (!(x == x && y == y && z == z && pitch == pitch && yaw == yaw)) continue;
        float dx = x / 100.0f - pawn_pos[0];
        float dy = y / 100.0f - pawn_pos[1];
        float dz = z / 100.0f - pawn_pos[2];
        if (dx * dx + dy * dy + dz * dz > 100.0f) continue;
        if (fabsf(pitch) > 10000.0f || fabsf(yaw) > 10000.0f) continue;
        int cache_off = location_off - OFF_CAMERA_CACHE_POV;
        int duplicate = 0;
        for (int i = 0; i < camera_candidate_count; i++)
            if (camera_candidate_offsets[i] == cache_off) { duplicate = 1; break; }
        if (duplicate) continue;
        camera_candidate_offsets[camera_candidate_count] = cache_off;
        camera_candidate_timestamps[camera_candidate_count] =
            read_float(camera_manager + (uint64_t)cache_off);
        camera_candidate_count++;
    }
}

static int select_advancing_camera_cache(uint64_t camera_manager) {
    for (int i = 0; i < camera_candidate_count; i++) {
        float timestamp = read_float(camera_manager + (uint64_t)camera_candidate_offsets[i]);
        float previous = camera_candidate_timestamps[i];
        camera_candidate_timestamps[i] = timestamp;
        if (timestamp == timestamp && previous == previous &&
            fabsf(timestamp - previous) > 0.000001f)
            return camera_candidate_offsets[i];
    }
    return -1;
}

/* Read entities using the discovered offsets */
static int read_entities_sdk(uint64_t gworld_addr, int actor_source,
                             int root_off, int pos_off, int use_ctw) {
    (void)use_ctw; /* ComponentToWorld mode is retained for CLI compatibility. */
    diagnostic_read_failures = 0;
    diagnostic_failure_address = 0;
    diagnostic_failure_errno = 0;
    diagnostic_gworld = gworld_addr;
    diagnostic_world = diagnostic_level = diagnostic_actor_data = 0;
    diagnostic_actor_count = 0;
    diagnostic_status = "world unavailable";
    local_player_controlled = 0;
    uint64_t uworld = read_ptr(gworld_addr);
    if (!is_any_ptr(uworld)) return -1;
    diagnostic_world = uworld;
    if (verified_uworld != uworld) {
        verified_uworld = uworld;
        match_was_verified = 0;
        last_camera_timestamp = -1.0f;
        stale_camera_frames = 0;
        health_layout_count = 0;
        health_layout_clock = 0;
    }

    diagnostic_status = "level unavailable";
    uint64_t level = read_ptr(uworld + OFF_UWORLD_PERSISTENT_LEVEL);
    if (!is_any_ptr(level)) return -1;
    diagnostic_level = level;

    uint64_t actor_data;
    int32_t actor_count;

    diagnostic_status = "actor array unavailable";
    if (actor_source == 0) {
        uint64_t cluster = read_ptr(level + OFF_ULEVEL_ACTOR_CLUSTER);
        if (!is_any_ptr(cluster)) return -1;
        actor_data = read_ptr(cluster + OFF_ACTOR_CONTAINER_ACTORS);
        actor_count = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 8);
    } else {
        int off = ULEVEL_ACTOR_OFFSETS[actor_source - 1];
        actor_data = read_ptr(level + off);
        actor_count = read_i32(level + off + 8);
    }

    diagnostic_actor_data = actor_data;
    diagnostic_actor_count = actor_count;
    if (!is_any_ptr(actor_data) || actor_count <= 0 || actor_count > 50000) return -1;
    diagnostic_status = "world sampled";

    self_entity_id[0] = 0;
    self_verified = 0;

    /* Find local player pawn via the matching Engine SDK layout:
     * UWorld+0x190 -> GI+0x38 -> LP[0]+0x30 -> PC+0x3f0 -> AcknowledgedPawn. */
    uint64_t local_pawn = 0;
    uint64_t local_pc = 0;
    uint64_t local_root = 0;
    uint64_t local_class = 0;
    camera_position_plaintext = 0;
    camera_sampled_at_ms = 0;
    int camera_valid = 0;
    float camera_x = 0, camera_y = 0, camera_z = 0;
    float camera_pitch = 0, camera_yaw = 0, camera_fov = 0;
    {
        uint64_t gi = read_ptr(uworld + 0x190);
        if (is_any_ptr(gi)) {
            uint64_t lp_arr = read_ptr(gi + 0x38);
            if (is_any_ptr(lp_arr)) {
                uint64_t lp0 = read_ptr(lp_arr);
                if (is_any_ptr(lp0)) {
                    uint64_t pc = read_ptr(lp0 + 0x30);
                    if (is_any_ptr(pc)) {
                        local_pc = pc;
                        uint64_t pawn = read_ptr(
                            pc + OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN);
                        if (is_any_ptr(pawn)) {
                            local_pawn = pawn;
                            local_class = read_ptr(pawn + OFF_UOBJECT_CLASS);
                            uint64_t pawn_root = read_ptr(pawn + root_off);
                            if (is_any_ptr(pawn_root)) {
                                local_root = pawn_root;
                                cached_self_pos[0] = read_float(pawn_root + pos_off) / 100.0f;
                                cached_self_pos[1] = read_float(pawn_root + pos_off + 4) / 100.0f;
                                cached_self_pos[2] = read_float(pawn_root + pos_off + 8) / 100.0f;
                            }
                            cached_local_class = local_class;
                        }
                    }
                }
            }
        }
    }

    /* The rendered camera is not the pawn origin (and changes target while
     * spectating). Read the engine's current camera cache so projection follows
     * hills, stance, camera pitch, and spectator transitions exactly. */
    if (is_any_ptr(local_pc)) {
        if (resolved_camera_manager_off < 0) {
            resolved_camera_manager_off = find_object_field(
                local_pc, 0x300, 0x800, "PlayerCameraManager");
            if (resolved_camera_manager_off < 0)
                resolved_camera_manager_off = find_camera_manager_field(local_pc);
            if (resolved_camera_manager_off >= 0)
                fprintf(stderr, "\n[+] Located PlayerCameraManager at controller+0x%x\n",
                        resolved_camera_manager_off);
        }
        int camera_manager_off = resolved_camera_manager_off >= 0 ?
            resolved_camera_manager_off : OFF_PC_CAMERA_MANAGER;
        uint64_t camera_manager = read_ptr(local_pc + (uint64_t)camera_manager_off);
        static int camera_diag_logged = 0;
        if (!camera_diag_logged) {
            uint64_t cache = camera_manager + OFF_CAMERA_CACHE_PRIVATE;
            uint64_t pov = cache + OFF_CAMERA_CACHE_POV;
            fprintf(stderr,
                "\n[diag] camera pc=0x%lx managerOff=0x%x manager=0x%lx "
                "sdkCache(ts=%.3f loc=%.1f,%.1f,%.1f rot=%.1f,%.1f fov=%.1f)\n",
                (unsigned long)local_pc, camera_manager_off,
                (unsigned long)camera_manager, read_float(cache),
                read_float(pov), read_float(pov + 4), read_float(pov + 8),
                read_float(pov + OFF_POV_ROTATION),
                read_float(pov + OFF_POV_ROTATION + 4),
                read_float(pov + OFF_POV_FOV));
            camera_diag_logged = 1;
        }
        if (is_telemetry_ptr(camera_manager)) {
            if (resolved_camera_cache_off < 0) {
                if (camera_candidate_count == 0)
                    discover_camera_caches(camera_manager, cached_self_pos);
                else
                    resolved_camera_cache_off = select_advancing_camera_cache(camera_manager);
                if (resolved_camera_cache_off >= 0)
                    fprintf(stderr, "[+] Located live camera cache at manager+0x%x\n",
                            resolved_camera_cache_off);
            }
            if (resolved_camera_cache_off < 0) goto camera_done;
            uint64_t cache = camera_manager + (uint64_t)resolved_camera_cache_off;
            uint64_t pov = cache + OFF_CAMERA_CACHE_POV;
            float timestamp = read_float(cache);
            uint8_t camera_vector[16];
            float camera_location[3];
            if (read_mem(pov + OFF_POV_LOCATION, camera_vector, sizeof(camera_vector)) !=
                sizeof(camera_vector)) goto camera_done;
            camera_position_plaintext = enc_handler_unmanaged(camera_vector + 0x0C);
            memcpy(camera_location, camera_vector, sizeof(camera_location));
            camera_x = camera_location[0] / 100.0f;
            camera_y = camera_location[1] / 100.0f;
            camera_z = camera_location[2] / 100.0f;
            camera_pitch = read_float(pov + OFF_POV_ROTATION);
            camera_yaw = read_float(pov + OFF_POV_ROTATION + 4);
            camera_fov = read_float(pov + OFF_POV_FOV);
            struct timespec camera_sample_time;
            clock_gettime(CLOCK_REALTIME, &camera_sample_time);
            camera_sampled_at_ms = (long long)camera_sample_time.tv_sec * 1000 +
                camera_sample_time.tv_nsec / 1000000;
            camera_valid = camera_x == camera_x && camera_y == camera_y && camera_z == camera_z &&
                camera_pitch == camera_pitch && camera_yaw == camera_yaw &&
                fabsf(camera_x) < 100000.0f && fabsf(camera_y) < 100000.0f &&
                fabsf(camera_z) < 100000.0f && camera_fov >= 5.0f && camera_fov <= 180.0f;
            if (camera_valid) {
                if (timestamp == last_camera_timestamp) stale_camera_frames++;
                else { last_camera_timestamp = timestamp; stale_camera_frames = 0; }
                /* UE stops advancing CameraCache when the game is backgrounded. */
                if (stale_camera_frames > 45) camera_valid = 0;
            }
        }
    }
camera_done:

    local_player_controlled = pawn_is_controlled(local_pawn, local_pc);

    if (!is_any_ptr(local_pawn) || !is_any_ptr(local_class) || !is_any_ptr(local_root)) {
        if (!is_any_ptr(local_pc) || !is_any_ptr(cached_local_class)) return 0;
        local_class = cached_local_class;
    }

    /* The ULevel actor list and class hierarchy are expensive to traverse and
     * change far less often than transforms. Discover verified character pawns
     * once per second, then read only their real RootComponent transforms on
     * the 30 Hz camera/update path. */
    static uint64_t cached_uworld = 0;
    static uint64_t cached_actor_data = 0;
    static uint64_t cached_actor_ptrs[MAX_ACTORS];
    static uint64_t cached_actor_classes[MAX_ACTORS];
    static uint8_t cached_actor_kinds[MAX_ACTORS];
    static int cached_actor_count = 0;
    static unsigned cache_age = 0;
    int refresh_cache = cached_uworld != uworld || cached_actor_data != actor_data ||
                        cached_actor_count == 0 || cache_age >= 20;
    if (refresh_cache) {
        uint64_t levels[512] = {0};
        int level_count = 1;
        levels[0] = level;

        /* Remote human pawns may be owned by streamed levels while bots and
         * the local pawn remain in PersistentLevel. Walk UWorld::Levels and
         * merge their actor arrays into one deduplicated transform cache. */
        uint64_t levels_data = read_ptr(uworld + OFF_UWORLD_LEVELS);
        int32_t world_level_count = read_i32(uworld + OFF_UWORLD_LEVELS + 8);
        if (is_any_ptr(levels_data) && world_level_count > 0 && world_level_count <= 511) {
            uint64_t streamed[511] = {0};
            if (read_mem(levels_data, streamed, (size_t)world_level_count * 8) ==
                (ssize_t)((size_t)world_level_count * 8)) {
                for (int li = 0; li < world_level_count && level_count < 512; li++) {
                    if (!is_any_ptr(streamed[li])) continue;
                    int duplicate = 0;
                    for (int existing = 0; existing < level_count; existing++)
                        if (levels[existing] == streamed[li]) { duplicate = 1; break; }
                    if (!duplicate) levels[level_count++] = streamed[li];
                }
            }
        } else if (world_level_count > 511) {
            static unsigned level_warnings = 0;
            if ((level_warnings++ % 300) == 0)
                fprintf(stderr, "[!] Too many streamed levels (%d); scanning persistent level only\n",
                        world_level_count);
        }

        uint64_t *all_actor_ptrs = (uint64_t *)(read_buf + READ_BUF_SIZE / 2);
        uint64_t loot_ptrs[MAX_ACTORS];
        uint64_t loot_actor_classes[MAX_ACTORS];
        uint8_t loot_kinds[MAX_ACTORS];
        struct { uint64_t cls; int kind; } loot_classes[512];
        int loot_count = 0, loot_class_count = 0;
        cached_actor_count = 0;
        for (int li = 0; li < level_count && cached_actor_count < MAX_ACTORS; li++) {
            uint64_t level_actor_data = 0;
            int32_t level_actor_count = 0;
            if (actor_source == 0) {
                uint64_t cluster = read_ptr(levels[li] + OFF_ULEVEL_ACTOR_CLUSTER);
                if (!is_any_ptr(cluster)) continue;
                level_actor_data = read_ptr(cluster + OFF_ACTOR_CONTAINER_ACTORS);
                level_actor_count = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 8);
            } else {
                int off = ULEVEL_ACTOR_OFFSETS[actor_source - 1];
                level_actor_data = read_ptr(levels[li] + off);
                level_actor_count = read_i32(levels[li] + off + 8);
            }
            if (!is_any_ptr(level_actor_data) || level_actor_count <= 0 || level_actor_count > 50000)
                continue;
            int scan_count = level_actor_count < 2000 ? level_actor_count : 2000;
            if (read_mem(level_actor_data, all_actor_ptrs, (size_t)scan_count * 8) !=
                (ssize_t)((size_t)scan_count * 8)) continue;

            for (int i = 0; i < scan_count && cached_actor_count < MAX_ACTORS; i++) {
                uint64_t actor = all_actor_ptrs[i];
                if (!is_any_ptr(actor)) continue;
                int already_cached = 0;
                for (int ci = 0; ci < cached_actor_count; ci++)
                    if (cached_actor_ptrs[ci] == actor) { already_cached = 1; break; }
                if (already_cached) continue;

                uint64_t cls = read_ptr(actor + OFF_UOBJECT_CLASS);
                int is_character = actor == local_pawn || is_character_class(cls, local_class);

                /* Human operators do not always share the local pawn's concrete
                 * class hierarchy (different operator/skin blueprints). A named
                 * PlayerState is a fallback, but only if the actor also looks
                 * like a real pawn (has a CapsuleComponent root). Without
                 * this check, APlayerState actors pass the name test and
                 * produce ghost boxes at wrong positions. */
                if (!is_character) {
                    /* Structural pawn check: a real character pawn has a
                     * root component with a valid capsule half-height at
                     * the known offset (0.5-2.5m). APlayerState and other
                     * non-pawn actors won't have this. */
                    uint64_t root = read_ptr(actor + root_off);
                    int looks_like_pawn = 0;
                    if (is_any_ptr(root)) {
                        float hh = read_float(root + OFF_CAPSULE_COMPONENT_HALF_HEIGHT) / 100.0f;
                        looks_like_pawn = (hh == hh && hh >= 0.4f && hh <= 3.0f);
                    }
                    if (looks_like_pawn) {
                        uint64_t state = read_ptr(actor + OFF_APAWN_PLAYER_STATE);
                        if (is_telemetry_ptr(state)) {
                            char name[64] = {0};
                            read_fstring(state + OFF_PLAYER_STATE_NAME, name, sizeof(name));
                            if (!name[0])
                                read_fstring(state + OFF_PLAYER_STATE_NAME_PRIVATE, name, sizeof(name));
                            is_character = name[0] != '\0' &&
                                read_ptr(state + OFF_PLAYER_STATE_PAWN_PRIVATE) == actor;
                        }
                    }
                }

                if (is_character) {
                    cached_actor_ptrs[cached_actor_count] = actor;
                    cached_actor_classes[cached_actor_count] = cls;
                    cached_actor_kinds[cached_actor_count++] = LOOT_NONE;
                    continue;
                }

                uint64_t state = read_ptr(actor + OFF_APAWN_PLAYER_STATE);
                if (is_telemetry_ptr(state) &&
                    read_ptr(state + OFF_PLAYER_STATE_PAWN_PRIVATE) == actor)
                    continue;
                int loot_kind = -1;
                for (int ci = 0; ci < loot_class_count; ci++)
                    if (loot_classes[ci].cls == cls) {
                        loot_kind = loot_classes[ci].kind;
                        break;
                    }
                if (loot_kind < 0 && is_any_ptr(cls)) {
                    loot_kind = loot_kind_from_class(cls);
                    if (loot_kind >= 0 && loot_class_count < 512) {
                        loot_classes[loot_class_count].cls = cls;
                        loot_classes[loot_class_count++].kind = loot_kind;
                    } else if (loot_kind < 0) {
                        static unsigned class_errors = 0;
                        if (class_errors++ < 5)
                            fprintf(stderr, "[!] Cannot validate loot class ancestry\n");
                    }
                }
                if (loot_kind > LOOT_NONE && loot_count < MAX_ACTORS) {
                    int duplicate = 0;
                    for (int li2 = 0; li2 < loot_count; li2++)
                        if (loot_ptrs[li2] == actor) { duplicate = 1; break; }
                    if (!duplicate) {
                        loot_ptrs[loot_count] = actor;
                        loot_actor_classes[loot_count] = cls;
                        loot_kinds[loot_count++] = (uint8_t)loot_kind;
                    }
                }
            }
        }
        for (int i = 0; i < loot_count && cached_actor_count < MAX_ACTORS; i++) {
            int duplicate = 0;
            for (int ci = 0; ci < cached_actor_count; ci++)
                if (cached_actor_ptrs[ci] == loot_ptrs[i]) { duplicate = 1; break; }
            if (duplicate) continue;
            cached_actor_ptrs[cached_actor_count] = loot_ptrs[i];
            cached_actor_classes[cached_actor_count] = loot_actor_classes[i];
            cached_actor_kinds[cached_actor_count++] = loot_kinds[i];
        }
        cached_uworld = uworld;
        cached_actor_data = actor_data;
        cache_age = 0;
    } else {
        cache_age++;
    }

    uint64_t *actor_ptrs = cached_actor_ptrs;
    int to_read = cached_actor_count;
    num_entities = 0;

    /* Collect positions for duplicate detection */
    float pos_list[MAX_ACTORS][3];
    int pos_count = 0;

    int player_num = 0;
    int diag_actor_ptr = 0;
    int diag_controller = 0;
    int diag_player_state = 0;
    int diag_root = 0;
    int diag_finite_position = 0;
    int diag_non_origin = 0;
    for (int i = 0; i < to_read && num_entities < MAX_ACTORS; i++) {
        if (!is_any_ptr(actor_ptrs[i])) continue;
        /* Actor storage can be recycled before the next discovery pass. This
         * identity check applies to characters as well as loot. */
        if (read_ptr(actor_ptrs[i] + OFF_UOBJECT_CLASS) != cached_actor_classes[i]) {
            cache_age = 20;
            continue;
        }
        diag_actor_ptr++;

        int is_self_pawn = (local_pawn != 0 && actor_ptrs[i] == local_pawn);
        diag_controller++;

        if (cached_actor_kinds[i] != LOOT_NONE) {
            uint64_t loot_root = read_ptr(actor_ptrs[i] + root_off);
            float position[3];
            if (!is_any_ptr(loot_root) || !read_plain_world_position(loot_root, position, NULL)) continue;
            Entity *loot = &entities[num_entities++];
            memset(loot, 0, sizeof(*loot));
            loot->class_address = cached_actor_classes[i];
            snprintf(loot->id, sizeof(loot->id), "ent_%llx",
                     (unsigned long long)actor_ptrs[i]);
            snprintf(loot->kind, sizeof(loot->kind), "loot");
            const char *name = cached_actor_kinds[i] == LOOT_DEATH_BOX ? "Death Box" :
                cached_actor_kinds[i] == LOOT_CONTAINER ? "Loot Container" :
                cached_actor_kinds[i] == LOOT_STASH ? "Stash / Interactable" : "Loot Item";
            snprintf(loot->name, sizeof(loot->name), "%s", name);
            loot->x = position[0] / 100.0f;
            loot->y = position[1] / 100.0f;
            loot->z = position[2] / 100.0f;
            loot->half_height = 0.35f;
            loot->team = 2;
            loot->native_team = -1;
            continue;
        }

        /* APawn::PlayerState is explicit in the matching UE4 SDK layout. The old
         * heuristic searched unrelated fields and mislabeled real pawns as loot. */
        uint64_t player_state = read_ptr(actor_ptrs[i] + OFF_APAWN_PLAYER_STATE);
        uint8_t bot_flags = 0;
        int has_playerstate = is_telemetry_ptr(player_state);
        if (has_playerstate &&
            read_mem(player_state + OFF_PLAYER_STATE_BOT_FLAGS, &bot_flags, 1) != 1) {
            static unsigned state_read_errors = 0;
            if (state_read_errors++ < 5)
                fprintf(stderr, "[!] Cannot read PlayerState flags for actor 0x%lx\n",
                        (unsigned long)actor_ptrs[i]);
            continue;
        }
        if (has_playerstate && !is_self_pawn) {
            uint64_t active_pawn = read_ptr(player_state + OFF_PLAYER_STATE_PAWN_PRIVATE);
            if (is_telemetry_ptr(active_pawn) && active_pawn != actor_ptrs[i]) continue;
        }
        diag_player_state++;

        uint64_t root = read_ptr(actor_ptrs[i] + root_off);
        if (!is_any_ptr(root)) continue;
        diag_root++;

        float position[3];
        int position_plaintext = 0;
        PositionObservation position_observation;
        int raw_position_read = read_actor_position(root, pos_off, position, &position_plaintext, &position_observation);
        if (!position_plaintext && pos_off == OFF_PROXIMA_ROOT_WORLD_LOCATION &&
            position_decode_actor(actor_ptrs[i], root, position)) {
            /* The legacy wire flag describes the emitted coordinates. The raw
             * source remains tagged/encrypted in positionTrace. */
            position_plaintext = 1;
            position_observation.decoded = 1;
        } else if (!raw_position_read) continue;
        float wx = position[0], wy = position[1], wz = position[2];

        if (wx != wx || wy != wy || wz != wz) continue;
        if (fabsf(wx) > 10000000 || fabsf(wy) > 10000000 || fabsf(wz) > 10000000) continue;
        diag_finite_position++;
        if (wx == 0 && wy == 0 && wz == 0) continue;

        /* Convert centimeters to meters */
        float mx = wx / 100.0f, my = wy / 100.0f, mz = wz / 100.0f;

        /* Filter: skip entities too close to world origin (< 20m) — these are uninitialized */
        float mag = sqrtf(mx*mx + my*my + mz*mz);
        if (mag < 20.0f) continue;
        {
            if (cached_self_pos[0] != 0 && fabsf(mz - cached_self_pos[2]) > 500.0f) continue;
        }
        diag_non_origin++;

        /* Filter: skip duplicate positions (within 0.5m of an existing entity) */
        int is_dup = 0;
        for (int d = 0; d < pos_count; d++) {
            float ddx = mx - pos_list[d][0];
            float ddy = my - pos_list[d][1];
            float ddz = mz - pos_list[d][2];
            if (ddx*ddx + ddy*ddy + ddz*ddz < 0.25f) { is_dup = 1; break; }
        }
        if (is_dup) continue;

        if (pos_count < MAX_ACTORS) {
            pos_list[pos_count][0] = mx;
            pos_list[pos_count][1] = my;
            pos_list[pos_count][2] = mz;
            pos_count++;
        }

        Entity *e = &entities[num_entities];
        memset(e, 0, sizeof(*e));
        e->class_address = cached_actor_classes[i];
        snprintf(e->id, sizeof(e->id), "ent_%llx", (unsigned long long)actor_ptrs[i]);

        /* Resolve class name and actor name via FName */
        char cls_name[64] = {0};
        char fname[64] = {0};
        if (fname_pool_addr > 0) {
            int32_t name_idx = read_i32(actor_ptrs[i] + OFF_UOBJECT_NAME);
            resolve_fname(name_idx, fname, sizeof(fname));
            uint64_t cls = read_ptr(actor_ptrs[i] + OFF_UOBJECT_CLASS);
            if (is_any_ptr(cls)) {
                int32_t cls_name_idx = read_i32(cls + OFF_UOBJECT_NAME);
                resolve_fname(cls_name_idx, cls_name, sizeof(cls_name));
            }
            snprintf(e->class_name, sizeof(e->class_name), "%s", cls_name);
        }
        char player_name[64] = {0};
        if (has_playerstate) {
            read_fstring(player_state + OFF_PLAYER_STATE_NAME, player_name, sizeof(player_name));
            if (!player_name[0])
                read_fstring(player_state + OFF_PLAYER_STATE_NAME_PRIVATE, player_name, sizeof(player_name));
        }
        if (player_name[0]) {
            strncpy(e->name, player_name, sizeof(e->name) - 1);
        } else if (has_playerstate) {
            player_num++;
            snprintf(e->name, sizeof(e->name), (bot_flags & 0x8) ? "Bot %d" : "Player %d", player_num);
        } else if (fname[0]) {
            strncpy(e->name, fname, sizeof(e->name) - 1);
        } else {
            player_num++;
            snprintf(e->name, sizeof(e->name), "Bot %d", player_num);
        }

        strncpy(e->kind, has_playerstate && !(bot_flags & 0x8) ? "player" : "ai", sizeof(e->kind) - 1);
        e->native_team = -1;
        if (has_playerstate &&
            read_mem(player_state + OFF_GP_PLAYER_STATE_TEAM_ID, &e->native_team,
                     sizeof(e->native_team)) != sizeof(e->native_team)) {
            static unsigned team_read_errors = 0;
            if (team_read_errors++ < 5)
                fprintf(stderr, "[!] Cannot read PlayerState team for actor 0x%lx\n",
                        (unsigned long)actor_ptrs[i]);
            e->native_team = -1;
        }

        e->x = mx;
        e->y = my;
        e->z = mz;
        e->position_plaintext = position_plaintext;
        e->position_observation = position_observation;
        e->rendered_on_screen = actor_rendered_on_screen(actor_ptrs[i]);
        e->yaw = read_float(root + OFF_USCENE_RELATIVE_YAW);
        e->pitch = read_float(root + OFF_USCENE_RELATIVE_PITCH);
        /* Matching SDK chain:
         * AGPCharacterBase::HealthComp -> UGPHealthDataComponent::HealthSet ->
         * FGPGameplayAttributeData::{Health,MaxHealth}.CurrentValue. */
        e->health = 0.0f;
        e->max_health = 0.0f;
        e->health_known = 0;
        HealthLayout *health_layout = health_layout_for_actor(actor_ptrs[i]);
        int health_comp_off = health_layout && health_layout->component_off >= 0 ?
            health_layout->component_off : OFF_CHARACTER_HEALTH_COMPONENT;
        uint64_t health_comp = read_ptr(actor_ptrs[i] + (uint64_t)health_comp_off);
        int health_set_off = health_layout && health_layout->set_off >= 0 ?
            health_layout->set_off : OFF_HEALTH_COMPONENT_SET;
        uint64_t health_set = is_game_heap_ptr(health_comp) ?
            read_ptr(health_comp + (uint64_t)health_set_off) : 0;
        float is_alive = 1.0f;
        {
            float hp = 0.0f, max_hp = 0.0f, alive = 1.0f;
            if (read_health_set(health_set, &hp, &max_hp, &alive)) {
                e->health = hp;
                e->max_health = max_hp;
                e->health_known = 1;
                is_alive = alive;
            }
        }

        uint8_t character_dead = 0;
        uint8_t player_dead = 0;
        read_mem(actor_ptrs[i] + OFF_CHARACTER_LAST_IS_DEAD, &character_dead, 1);
        if (has_playerstate) read_mem(player_state + OFF_PLAYER_STATE_DEAD, &player_dead, 1);
        /* Replicated actor/PlayerState death bits are authoritative. A health
         * chain is accepted for drawing only; a bad regional-layout candidate
         * must never hide a visibly alive pawn. */
        e->dead = (character_dead & 1) != 0 || (player_dead & 1) != 0 ||
            (health_layout && health_layout->death_trusted && e->health_known &&
             is_alive < 0.5f && e->health <= 0.01f);

        /* RootComponent is the character capsule. Prefer the engine capsule's
         * actual scaled geometry; the mirrored character copy is version-sensitive. */
        float capsule_half_height = read_float(root + OFF_CAPSULE_COMPONENT_HALF_HEIGHT) / 100.0f;
        if (!(capsule_half_height == capsule_half_height &&
              capsule_half_height >= 0.5f && capsule_half_height <= 2.5f))
            capsule_half_height = read_float(
                actor_ptrs[i] + OFF_CHARACTER_CAPSULE_HALF_HEIGHT) / 100.0f;
        e->half_height = capsule_half_height == capsule_half_height &&
            capsule_half_height >= 0.5f && capsule_half_height <= 2.5f ? capsule_half_height : 1.05f;

        /* Resolve this actor's actual animated pose and asset hierarchy. */
        read_entity_bones(e, actor_ptrs[i], root);

        /* Identify self: match against local pawn pointer */
        if (is_self_pawn) {
            strncpy(self_entity_id, e->id, sizeof(self_entity_id) - 1);
            self_verified = player_name[0] != '\0';
            if (self_verified) match_was_verified = 1;
            strncpy(e->name, player_name[0] ? player_name : "You", sizeof(e->name) - 1);
            strncpy(e->kind, "player", sizeof(e->kind) - 1);
            if (!player_name[0]) player_num--;
            /* Use camera rotation from PlayerController instead of body rotation */
            if (camera_valid) {
                e->x = camera_x; e->y = camera_y; e->z = camera_z;
                e->yaw = camera_yaw; e->pitch = camera_pitch;
                e->fov = camera_fov;
            } else if (local_pc) {
                float cam_yaw = read_float(local_pc + OFF_PC_CONTROL_YAW);
                float cam_pitch = read_float(local_pc + OFF_PC_CONTROL_PITCH);
                e->yaw = fmodf(cam_yaw, 360.0f);
                e->pitch = fmodf(cam_pitch, 360.0f);
                if (e->yaw > 180.0f) e->yaw -= 360.0f;
                if (e->yaw < -180.0f) e->yaw += 360.0f;
                if (e->pitch > 180.0f) e->pitch -= 360.0f;
                if (e->pitch < -180.0f) e->pitch += 360.0f;
            }
            /* Re-read the world position for freshness. */
            if (!camera_valid && local_root) {
                e->x = read_float(local_root + pos_off) / 100.0f;
                e->y = read_float(local_root + pos_off + 4) / 100.0f;
                e->z = read_float(local_root + pos_off + 8) / 100.0f;
            }
        }

        /* Team: use native_team from PlayerState for real team membership.
         * Self is always team=1. Same native_team as self = teammate (1).
         * Different native_team or no PlayerState = enemy (2). */
        static int self_native_team = -1;
        if (is_self_pawn) {
            self_native_team = e->native_team;
            e->team = 1;
        } else if (e->native_team >= 0 && self_native_team >= 0 &&
                   e->native_team == self_native_team) {
            e->team = 1; /* teammate */
        } else {
            e->team = 2; /* enemy */
        }

        num_entities++;
    }

    /* Periodic entity diagnostics */
    {
        static unsigned diag_tick2 = 0;
        if ((diag_tick2++ % 90) == 0 && num_entities > 0) {
            fprintf(stderr, "\n[diag] self=(%.1f,%.1f,%.1f) entities=%d\n",
                    cached_self_pos[0], cached_self_pos[1], cached_self_pos[2], num_entities);
            for (int i = 0; i < num_entities && i < 16; i++) {
                Entity *e = &entities[i];
                fprintf(stderr,
                        "  [%s] kind=%s pos=(%.1f,%.1f,%.1f) hp=%.0f/%.0f hh=%.2f "
                        "dead=%d team=%d native=%d fov=%.1f name=%s\n",
                        e->id, e->kind, e->x, e->y, e->z, e->health,
                        e->max_health, e->half_height, e->dead, e->team,
                        e->native_team, e->fov, e->name);
            }
        }
    }

    if (num_entities == 0) {
        static unsigned zero_diag_tick = 0;
        if ((zero_diag_tick++ % 150) == 0) {
            fprintf(stderr,
                    "\n[diag] world=0x%lx level=0x%lx actors=%d valid=%d ctrl=%d "
                    "state=%d root=%d finite=%d nonorigin=%d pc=0x%lx pawn=0x%lx\n",
                    (unsigned long)uworld, (unsigned long)level, to_read,
                    diag_actor_ptr, diag_controller, diag_player_state, diag_root,
                    diag_finite_position, diag_non_origin,
                    (unsigned long)local_pc, (unsigned long)local_pawn);
        }
    }

    /* During deployment, death/spectating and pawn replacement,
     * AcknowledgedPawn may still point at a short-lived proxy while the real
     * named network pawn occupies the same location.  Treat the nearest named
     * player as self instead of emitting a second synthetic player at the
     * camera.  The old behaviour was the source of boxes floating beside the
     * user after respawn or resume. */
    if (self_entity_id[0] == 0 && camera_valid && num_entities > 0) {
        int nearest = -1;
        float nearest_d2 = 16.0f; /* four metre replacement/replication window */
        for (int i = 0; i < num_entities; i++) {
            Entity *candidate = &entities[i];
            if (strcmp(candidate->kind, "player") != 0 || !candidate->name[0] ||
                strcmp(candidate->name, "DefaultName") == 0) continue;
            float dx = candidate->x - cached_self_pos[0];
            float dy = candidate->y - cached_self_pos[1];
            float dz = candidate->z - cached_self_pos[2];
            float d2 = dx * dx + dy * dy + dz * dz;
            if (d2 < nearest_d2) { nearest_d2 = d2; nearest = i; }
        }
        if (nearest >= 0) {
            Entity *self = &entities[nearest];
            strncpy(self_entity_id, self->id, sizeof(self_entity_id) - 1);
            self->team = 1;
            self->x = camera_x; self->y = camera_y; self->z = camera_z;
            self->yaw = camera_yaw; self->pitch = camera_pitch;
            self->fov = camera_fov;
            self_verified = 1;
            match_was_verified = 1;
        }
    }

    /* Normalize native team IDs to the API's stable 1=self / 2=other model. */
    int self_native_team = -1;
    for (int i = 0; i < num_entities; i++) {
        if (strcmp(entities[i].id, self_entity_id) == 0) {
            self_native_team = entities[i].native_team;
            break;
        }
    }
    for (int i = 0; i < num_entities; i++) {
        if (strcmp(entities[i].id, self_entity_id) == 0) entities[i].team = 1;
        else if (self_native_team >= 0 && entities[i].native_team >= 0)
            entities[i].team = entities[i].native_team == self_native_team ? 1 : 2;
        else entities[i].team = 2;
    }

    /* Fallback self: if pawn not matched, create synthetic self from PC data */
    if (self_entity_id[0] == 0 && local_pc && num_entities > 0 && num_entities < MAX_ACTORS) {
        Entity *e = &entities[num_entities];
        memset(e, 0, sizeof(*e));
        e->class_address = 0;
        e->class_name[0] = 0;
        e->position_plaintext = 0;
        e->rendered_on_screen = 0;
        snprintf(e->id, sizeof(e->id), "self");
        strncpy(e->name, "Self", sizeof(e->name) - 1);
        strncpy(e->kind, "player", sizeof(e->kind) - 1);
        e->team = 1;
        e->native_team = -1;
        e->health = 0.0f;
        e->max_health = 0.0f;
        e->health_known = 0;
        e->half_height = 1.05f;
        e->dead = 0;
        e->fov = camera_valid ? camera_fov : 80.0f;
        strncpy(self_entity_id, e->id, sizeof(self_entity_id) - 1);

        float cam_yaw = camera_valid ? camera_yaw : read_float(local_pc + OFF_PC_CONTROL_YAW);
        float cam_pitch = camera_valid ? camera_pitch : read_float(local_pc + OFF_PC_CONTROL_PITCH);
        e->yaw = fmodf(cam_yaw, 360.0f);
        e->pitch = fmodf(cam_pitch, 360.0f);
        if (e->yaw > 180.0f) e->yaw -= 360.0f;
        if (e->yaw < -180.0f) e->yaw += 360.0f;
        if (e->pitch > 180.0f) e->pitch -= 360.0f;
        if (e->pitch < -180.0f) e->pitch += 360.0f;

        if (camera_valid) {
            e->x = camera_x; e->y = camera_y; e->z = camera_z;
        } else if (local_root) {
            e->x = read_float(local_root + pos_off) / 100.0f;
            e->y = read_float(local_root + pos_off + 4) / 100.0f;
            e->z = read_float(local_root + pos_off + 8) / 100.0f;
        } else {
            e->x = cached_self_pos[0];
            e->y = cached_self_pos[1];
            e->z = cached_self_pos[2];
        }
        self_verified = match_was_verified && camera_valid;
        num_entities++;
    }

    if (!camera_valid) self_verified = 0;

    return num_entities;
}

static int persistent_sock = -1;

static int get_radar_sock(void) {
    if (persistent_sock >= 0) return persistent_sock;
    int sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) return -1;
    struct sockaddr_un addr = {0};
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0'; /* Android/Linux abstract namespace. */
    memcpy(addr.sun_path + 1, RADAR_SOCKET_NAME, strlen(RADAR_SOCKET_NAME));
    socklen_t addr_len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
        1 + strlen(RADAR_SOCKET_NAME));
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(sock, (struct sockaddr *)&addr, addr_len) < 0) {
        close(sock);
        return -1;
    }
    persistent_sock = sock;
    return sock;
}

static void close_radar_sock(void) {
    if (persistent_sock >= 0) { close(persistent_sock); persistent_sock = -1; }
}

static int format_diagnostics(char *buffer, size_t size) {
    return snprintf(buffer, size,
        "\"diagnostics\":{\"status\":\"%s\",\"gworld\":\"0x%llx\","
        "\"world\":\"0x%llx\",\"level\":\"0x%llx\",\"actorArray\":\"0x%llx\","
        "\"actorCount\":%d,\"readFailures\":%u,\"failureAddress\":\"0x%llx\","
        "\"failureErrno\":%d}",
        diagnostic_status, (unsigned long long)diagnostic_gworld,
        (unsigned long long)diagnostic_world, (unsigned long long)diagnostic_level,
        (unsigned long long)diagnostic_actor_data, diagnostic_actor_count,
        diagnostic_read_failures, (unsigned long long)diagnostic_failure_address,
        diagnostic_failure_errno);
}

static int post_diagnostics(void) {
    int sock = get_radar_sock();
    if (sock < 0) return -1;
    char body[1024];
    body[0] = '{';
    int length = format_diagnostics(body + 1, sizeof(body) - 3);
    if (length < 0 || (size_t)length >= sizeof(body) - 3) {
        fprintf(stderr, "[!] Diagnostic snapshot exceeds buffer\n");
        return -1;
    }
    body[length + 1] = '}';
    body[length + 2] = '\n';
    ssize_t sent = send(sock, body, (size_t)length + 3, MSG_NOSIGNAL);
    if (sent != length + 3) {
        fprintf(stderr, "[!] Diagnostic send failed: %s\n", strerror(errno));
        close_radar_sock();
        return -1;
    }
    return 0;
}

static int post_to_radar(void) {
    int sock = get_radar_sock();
    if (sock < 0) return -1;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long now_ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    char body[131072];
    int off = 0;
#define APPEND_SNAPSHOT(...) do { \
    int written = snprintf(body + off, sizeof(body) - (size_t)off, __VA_ARGS__); \
    if (written < 0 || (size_t)written >= sizeof(body) - (size_t)off) { \
        fprintf(stderr, "[!] Entity snapshot exceeds buffer\n"); \
        return -1; \
    } \
    off += written; \
} while (0)
    body[off++] = '{';
    int diagnostic_length = format_diagnostics(body + off, sizeof(body) - (size_t)off);
    if (diagnostic_length < 0 || (size_t)diagnostic_length >= sizeof(body) - (size_t)off) {
        fprintf(stderr, "[!] Entity diagnostics exceed buffer\n");
        return -1;
    }
    off += diagnostic_length;
    APPEND_SNAPSHOT(
        ",\"cameraSampledAt\":%lld,\"cameraTimestamp\":%.6f,\"cameraPositionPlaintext\":%s,\"localPlayerControlled\":%s,\"self\":\"%s\",\"map\":{\"name\":\"delta_force\",\"width\":10000,\"height\":10000},\"entities\":[",
        camera_sampled_at_ms, (double)last_camera_timestamp, camera_position_plaintext ? "true" : "false",
        local_player_controlled ? "true" : "false", self_entity_id[0] ? self_entity_id : "");
    int i;
    for (i = 0; i < num_entities; i++) {
        int bone_budget = entities[i].has_bones && entities[i].bone_count > 0 &&
            entities[i].bone_count <= MAX_SKELETON_BONES ? entities[i].bone_count * 48 : 0;
        if (off > (int)sizeof(body) - 8192 - bone_budget) break;
        if (i > 0) body[off++] = ',';
        char escaped_name[256];
        char escaped_class[256];
        json_escape(entities[i].name, escaped_name, sizeof(escaped_name));
        json_escape(entities[i].class_name, escaped_class, sizeof(escaped_class));
        APPEND_SNAPSHOT(
            "{\"id\":\"%s\",\"kind\":\"%s\",\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,"
            "\"yaw\":%.4f,\"pitch\":%.4f,\"fov\":%.1f,\"name\":\"%s\",\"health\":%.1f,\"maxHealth\":%.1f,"
            "\"className\":\"%s\",\"classAddress\":\"0x%llx\","
            "\"healthKnown\":%s,\"positionPlaintext\":%s,\"positionDecoded\":%s,\"renderedOnScreen\":%s,"
            "\"halfHeight\":%.2f,\"dead\":%s,\"team\":%d,\"lastSeen\":%lld",
            entities[i].id, entities[i].kind[0] ? entities[i].kind : "player",
            entities[i].x, entities[i].y, entities[i].z,
            entities[i].yaw, entities[i].pitch, entities[i].fov, escaped_name, entities[i].health,
            entities[i].max_health, escaped_class, (unsigned long long)entities[i].class_address,
            entities[i].health_known ? "true" : "false",
            entities[i].position_plaintext ? "true" : "false",
            entities[i].position_observation.decoded ? "true" : "false",
            entities[i].rendered_on_screen ? "true" : "false",
            entities[i].half_height, entities[i].dead ? "true" : "false",
            entities[i].team, now_ms);
        if (entities[i].position_observation.present) {
            const PositionObservation *observation = &entities[i].position_observation;
            APPEND_SNAPSHOT(
                ",\"positionTrace\":{\"root\":\"0x%llx\",\"boundsX\":%.3f,\"boundsY\":%.3f,"
                "\"boundsZ\":%.3f,\"boundsIndex\":%d,\"boundsEncrypted\":%d,\"boundsFlags\":%d,"
                "\"worldIndex\":%d,\"worldEncrypted\":%d,\"worldFlags\":%d}",
                (unsigned long long)observation->root,
                observation->bounds[0] / 100.0f, observation->bounds[1] / 100.0f,
                observation->bounds[2] / 100.0f, observation->bounds_index,
                observation->bounds_encrypted, observation->bounds_flags,
                observation->world_index, observation->world_encrypted, observation->world_flags);
        }
        APPEND_SNAPSHOT(",\"bonesVerified\":%s,\"boneCount\":%d", entities[i].has_bones ? "true" : "false", entities[i].bone_count);
        if (entities[i].has_bones && entities[i].bone_count >= 2 && entities[i].bone_count <= MAX_SKELETON_BONES) {
            APPEND_SNAPSHOT(",\"bones\":[");
            for (int b = 0; b < entities[i].bone_count; b++) {
                if (b > 0) body[off++] = ',';
                APPEND_SNAPSHOT(
                    "[%.2f,%.2f,%.2f]",
                    entities[i].bone_pos[b][0],
                    entities[i].bone_pos[b][1],
                    entities[i].bone_pos[b][2]);
            }
            APPEND_SNAPSHOT("],\"boneParents\":[");
            for (int b = 0; b < entities[i].bone_count; b++) {
                APPEND_SNAPSHOT("%s%d", b ? "," : "", (int)entities[i].bone_parent[b]);
            }
            APPEND_SNAPSHOT("]");
        }
        body[off++] = '}';
    }
    APPEND_SNAPSHOT("],\"entitiesTruncated\":%s}\n", i < num_entities ? "true" : "false");
#undef APPEND_SNAPSHOT

    ssize_t sent = send(sock, body, off, MSG_NOSIGNAL);
    if (sent != off) {
        fprintf(stderr, "\n[!] post send failed: %s\n", strerror(errno));
        close_radar_sock();
        return -1;
    }
    return 0;
}

static volatile int running = 1;
static void sig_handler(int sig) { (void)sig; running = 0; }

#define WORLD_CACHE_PATH "/data/local/tmp/delta-world.cache"

static int load_world_cache(int pid, uint64_t *gworld, int *actor_source) {
    FILE *f = fopen(WORLD_CACHE_PATH, "r");
    if (!f) return 0;
    int cached_pid = 0, cached_source = 0;
    unsigned long long cached_gworld = 0;
    int matched = fscanf(f, "%d %llx %d", &cached_pid, &cached_gworld, &cached_source) == 3;
    fclose(f);
    if (!matched || cached_pid != pid || cached_source < 0 || cached_source > 4) return 0;
    GWorldCandidate candidate = {0};
    uint64_t uworld = read_ptr((uint64_t)cached_gworld);
    if (!is_any_ptr(uworld) || try_validate_uworld(uworld, &candidate) < 130 ||
        candidate.plausible_players < 1) return 0;
    *gworld = (uint64_t)cached_gworld;
    *actor_source = cached_source;
    return 1;
}

static void save_world_cache(int pid, uint64_t gworld, int actor_source) {
    FILE *f = fopen(WORLD_CACHE_PATH, "w");
    if (!f) return;
    fprintf(f, "%d %llx %d\n", pid, (unsigned long long)gworld, actor_source);
    fclose(f);
}

int main(int argc, char **argv) {
    int mode_scan = 0, mode_run = 0, mode_regions = 0, mode_dump = 0;
    int use_root_off = OFF_AACTOR_ROOT_COMPONENT;
    int use_pos_off = OFF_PROXIMA_ROOT_WORLD_LOCATION;
    int use_actor_src = 0;
    int use_ctw = 0;
    uint64_t use_gworld = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "scan") == 0) mode_scan = 1;
        else if (strcmp(argv[i], "run") == 0) mode_run = 1;
        else if (strcmp(argv[i], "regions") == 0) mode_regions = 1;
        else if (strcmp(argv[i], "dump") == 0) mode_dump = 1;
        else if (strncmp(argv[i], "gworld=", 7) == 0)
            use_gworld = strtoull(argv[i] + 7, NULL, 16);
        else if (strncmp(argv[i], "root=", 5) == 0)
            use_root_off = strtol(argv[i] + 5, NULL, 16);
        else if (strncmp(argv[i], "pos=", 4) == 0)
            use_pos_off = strtol(argv[i] + 4, NULL, 16);
        else if (strncmp(argv[i], "actorsrc=", 9) == 0)
            use_actor_src = atoi(argv[i] + 9);
        else if (strcmp(argv[i], "ctw") == 0)
            use_ctw = 1;
        else if (strcmp(argv[i], "vmreadv") == 0)
            use_vm_readv = 1;
    }

    if (!mode_scan && !mode_run && !mode_regions && !mode_dump) {
        fprintf(stderr,
            "Usage: memscan <scan|run|regions|dump> [vmreadv] [options]\n"
            "  scan              - Find GWorld and probe offsets\n"
            "  run               - Continuously read entities and POST to radar\n"
            "    gworld=ADDR     - GWorld address (hex)\n"
            "    actorsrc=N      - Actor source (0=cluster, 1+=direct)\n"
            "    root=OFF        - RootComponent offset (default 0x180)\n"
            "    pos=OFF         - Position offset (default 0x164)\n"
            "    ctw             - Use ComponentToWorld instead of RelativeLocation\n"
            "  regions           - List memory regions\n"
            "  dump gworld=ADDR  - Dump UWorld structure\n"
            "  vmreadv           - Use process_vm_readv (recommended)\n");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    read_buf = malloc(READ_BUF_SIZE);
    if (!read_buf) { fprintf(stderr, "malloc failed\n"); return 1; }

    game_pid = find_game_pid();
    if (game_pid <= 0) { fprintf(stderr, "[-] Game not running\n"); free(read_buf); return 1; }
    fprintf(stderr, "[+] Game PID: %d\n", game_pid);

    if (!use_vm_readv) {
        char path[256];
        snprintf(path, sizeof(path), "/proc/%d/mem", game_pid);
        mem_fd = open(path, O_RDONLY);
        if (mem_fd < 0) {
            fprintf(stderr, "[-] Cannot open %s: %s\n", path, strerror(errno));
            free(read_buf); return 1;
        }
    } else {
        fprintf(stderr, "[+] Using process_vm_readv\n");
    }

    if (parse_maps_with_retry() != 0) {
        fprintf(stderr, "[-] Cannot read populated process maps for PID %d\n", game_pid);
        free(read_buf);
        return 1;
    }
    fprintf(stderr, "[+] %d regions, UE4: 0x%lx-0x%lx (%lu MB)\n",
            num_regions, (unsigned long)ue4_base, (unsigned long)ue4_end,
            (unsigned long)((ue4_end - ue4_base) / (1024*1024)));

    /* Try to set up FName resolution from known GNames offset.
     * The FNamePool::Blocks offset varies between UE4 builds; probe several. */
    if (ue4_base > 0) {
        uint64_t gnames_base = ue4_base + OFF_GNAMES_FROM_UE4_BASE;
        static const int blocks_offsets[] = {0x38, 0x10, 0x18, 0x20, 0x28, 0x30, 0x40, 0x48, 0x50};
        int found_blocks = 0;
        for (int bi = 0; bi < (int)(sizeof(blocks_offsets)/sizeof(blocks_offsets[0])) && !found_blocks; bi++) {
            uint64_t block0 = read_ptr(gnames_base + blocks_offsets[bi]);
            if (!is_any_ptr(block0)) continue;
            char test[16];
            if (read_mem(block0, test, 16) != 16) continue;
            for (int no = 2; no <= 8; no += 2) {
                if (memcmp(test + no, "None", 4) == 0) {
                    fname_pool_addr = gnames_base;
                    fname_blocks_off = blocks_offsets[bi];
                    fname_entry_hdr_size = no;
                    fprintf(stderr, "[+] GNames validated at 0x%lx (blocks@+0x%x, hdr=%d)\n",
                            (unsigned long)fname_pool_addr, fname_blocks_off, no);
                    found_blocks = 1;
                    break;
                }
            }
        }
        if (!found_blocks) {
            /* Scan the three large UE4 rw data sections for any pointer
             * whose target starts with the "None" FNameEntry. Read in
             * 64KB chunks; only valid-looking pointers get a remote
             * verify, so the costly read_mem calls stay proportional to
             * pointer density, not region size. */
            fprintf(stderr, "[*] Scanning UE4 data sections for FNamePool block0...\n");
            fname_pool_addr = 0;
            for (int ri = 0; ri < num_regions && !found_blocks; ri++) {
                if (regions[ri].perms[1] != 'w') continue;
                if (strstr(regions[ri].name, "libUE4") == NULL) continue;
                uint64_t sz = regions[ri].end - regions[ri].start;
                if (sz < 65536) continue; /* only the 3 big data sections */
                fprintf(stderr, "  [*] 0x%lx (%lu MB)\n",
                        (unsigned long)regions[ri].start, (unsigned long)(sz / (1024*1024)));
                for (uint64_t off = 0; off < sz && !found_blocks; off += 65536) {
                    uint64_t chunk_sz = sz - off;
                    if (chunk_sz > 65536) chunk_sz = 65536;
                    uint8_t buf[65536];
                    if (read_mem(regions[ri].start + off, buf, (size_t)chunk_sz) != (ssize_t)chunk_sz)
                        continue;
                    /* Scan for consecutive valid heap pointers where the
                     * first one leads to "None" — that's the blocks array. */
                    for (uint64_t p = 0; p + 16 <= chunk_sz && !found_blocks; p += 8) {
                        uint64_t ptr0, ptr1;
                        memcpy(&ptr0, buf + p, 8);
                        if (!is_any_ptr(ptr0)) continue;
                        memcpy(&ptr1, buf + p + 8, 8);
                        if (!is_any_ptr(ptr1)) continue;
                        /* Two consecutive heap pointers — check if ptr0 → "None"
                         * at various header sizes (2, 4, 6, 8 bytes) */
                        char test[16];
                        if (read_mem(ptr0, test, 16) != 16) continue;
                        int none_off = -1;
                        for (int no = 2; no <= 8; no += 2)
                            if (memcmp(test + no, "None", 4) == 0) { none_off = no; break; }
                        if (none_off < 0) continue;
                        /* Found block0! The blocks array is at this position.
                         * FNamePool structure starts some offset before. */
                        uint64_t blocks_addr = regions[ri].start + off + p;
                        /* Determine the offset from FNamePool start to blocks.
                         * Read backwards to find the pool header. For now, try
                         * common offsets by calculating the pool start. */
                        for (int bi = 0; bi < (int)(sizeof(blocks_offsets)/sizeof(blocks_offsets[0])); bi++) {
                            if ((int64_t)(off + p) - blocks_offsets[bi] < 0) continue;
                            fname_pool_addr = blocks_addr - blocks_offsets[bi];
                            fname_blocks_off = blocks_offsets[bi];
                            /* Verify: re-read block0 from our pool address */
                            uint64_t v = read_ptr(fname_pool_addr + fname_blocks_off);
                            if (v == ptr0) {
                                fname_entry_hdr_size = none_off;
                                fprintf(stderr, "[+] FNamePool at 0x%lx (blocks@+0x%x=%0lx, hdr=%d)\n",
                                        (unsigned long)fname_pool_addr, fname_blocks_off,
                                        (unsigned long)blocks_addr, none_off);
                                found_blocks = 1;
                                break;
                            }
                        }
                        if (!found_blocks) {
                            /* Direct: treat blocks_addr as pool + blocks_off=0 */
                            fname_pool_addr = blocks_addr;
                            fname_blocks_off = 0;
                            fname_entry_hdr_size = none_off;
                            fprintf(stderr, "[+] FNamePool blocks at 0x%lx (blocks@+0x0, hdr=%d)\n",
                                    (unsigned long)blocks_addr, none_off);
                            found_blocks = 1;
                        }
                    }
                }
            }
            /* Phase 3: Search ALL writable heap pages for the "None" string.
             * FNamePool block[0] is a heap allocation starting with a small header
             * then "None\0" as the very first FName entry (index 0). */
            if (!found_blocks) {
                fprintf(stderr, "[*] Phase 3: Searching heap for 'None' FNameEntry...\n");
                for (int ri = 0; ri < num_regions && !found_blocks; ri++) {
                    if (regions[ri].perms[0] != 'r' || regions[ri].perms[1] != 'w') continue;
                    uint64_t sz = regions[ri].end - regions[ri].start;
                    if (sz < 4096 || sz > 128 * 1024 * 1024) continue;
                    /* Skip libUE4 code sections (already scanned data above) */
                    if (strstr(regions[ri].name, "libUE4") != NULL && regions[ri].perms[2] == 'x') continue;
                    /* Read first 4KB of each region — block[0] is typically at the start of its page */
                    uint8_t page[4096];
                    if (read_mem(regions[ri].start, page, 4096) != 4096) continue;
                    for (int no = 2; no <= 8; no += 2) {
                        if (memcmp(page + no, "None", 4) == 0 && page[no + 4] == 0) {
                            /* This page starts with a FNameEntry for "None"!
                             * This is block[0]. Now find who points to it. */
                            uint64_t block0_addr = regions[ri].start;
                            fprintf(stderr, "[+] Found 'None' FNameEntry at 0x%lx+%d\n",
                                    (unsigned long)block0_addr, no);
                            /* Search UE4 data sections for a pointer to this block0 */
                            for (int ri2 = 0; ri2 < num_regions && !found_blocks; ri2++) {
                                if (regions[ri2].perms[1] != 'w') continue;
                                if (strstr(regions[ri2].name, "libUE4") == NULL) continue;
                                uint64_t sz2 = regions[ri2].end - regions[ri2].start;
                                if (sz2 < 65536) continue;
                                for (uint64_t off2 = 0; off2 < sz2 && !found_blocks; off2 += 65536) {
                                    uint64_t chunk2 = sz2 - off2;
                                    if (chunk2 > 65536) chunk2 = 65536;
                                    uint8_t buf2[65536];
                                    if (read_mem(regions[ri2].start + off2, buf2, (size_t)chunk2) != (ssize_t)chunk2)
                                        continue;
                                    for (uint64_t p2 = 0; p2 + 8 <= chunk2; p2 += 8) {
                                        uint64_t val;
                                        memcpy(&val, buf2 + p2, 8);
                                        if (val == block0_addr) {
                                            uint64_t ptr_addr = regions[ri2].start + off2 + p2;
                                            /* This pointer points to block0. Try different pool offsets. */
                                            for (int bi = 0; bi < (int)(sizeof(blocks_offsets)/sizeof(blocks_offsets[0])); bi++) {
                                                fname_pool_addr = ptr_addr - blocks_offsets[bi];
                                                fname_blocks_off = blocks_offsets[bi];
                                                fname_entry_hdr_size = no;
                                                uint64_t v = read_ptr(fname_pool_addr + fname_blocks_off);
                                                if (v == block0_addr) {
                                                    fprintf(stderr, "[+] FNamePool at 0x%lx (blocks@+0x%x, hdr=%d) [Phase 3]\n",
                                                            (unsigned long)fname_pool_addr, fname_blocks_off, no);
                                                    found_blocks = 1;
                                                    break;
                                                }
                                            }
                                            if (!found_blocks) {
                                                fname_pool_addr = ptr_addr;
                                                fname_blocks_off = 0;
                                                fname_entry_hdr_size = no;
                                                fprintf(stderr, "[+] FNamePool blocks at 0x%lx (blocks@+0x0, hdr=%d) [Phase 3]\n",
                                                        (unsigned long)ptr_addr, no);
                                                found_blocks = 1;
                                            }
                                        }
                                    }
                                }
                            }
                            if (found_blocks) break;
                        }
                    }
                }
            }
            if (!found_blocks)
                fprintf(stderr, "[!] FNamePool not found after all phases\n");
        }
    }

    if (mode_regions) {
        for (int i = 0; i < num_regions; i++) {
            uint64_t sz = regions[i].end - regions[i].start;
            if (sz < 1024 * 1024) continue;
            printf("0x%012lx-0x%012lx %s %6lu MB %s\n",
                   (unsigned long)regions[i].start, (unsigned long)regions[i].end,
                   regions[i].perms, (unsigned long)(sz / (1024*1024)), regions[i].name);
        }
        if (mem_fd >= 0) close(mem_fd);
        free(read_buf);
        return 0;
    }

    if (mode_dump && use_gworld != 0) {
        fprintf(stderr, "[*] Dumping around GWorld 0x%lx\n", (unsigned long)use_gworld);
        uint64_t uworld = read_ptr(use_gworld);
        fprintf(stderr, "[*] UWorld = 0x%lx\n", (unsigned long)uworld);
        if (is_any_ptr(uworld)) {
            fprintf(stderr, "[*] UWorld pointer dump:\n");
            for (int off = 0; off <= 0x300; off += 8) {
                uint64_t v = read_ptr(uworld + off);
                if (v != 0) {
                    fprintf(stderr, "  +0x%03x: 0x%016lx %s%s\n", off, (unsigned long)v,
                            is_heap_ptr(v) ? "(heap)" : is_any_ptr(v) ? "(ptr)" : "",
                            off == OFF_UWORLD_PERSISTENT_LEVEL ? " <-- PersistentLevel" :
                            off == OFF_UWORLD_GAME_STATE ? " <-- GameState" : "");
                }
            }
            /* Also dump PersistentLevel */
            uint64_t level = read_ptr(uworld + OFF_UWORLD_PERSISTENT_LEVEL);
            if (is_any_ptr(level)) {
                fprintf(stderr, "\n[*] PersistentLevel 0x%lx dump:\n", (unsigned long)level);
                for (int off = 0; off <= 0x200; off += 8) {
                    uint64_t v = read_ptr(level + off);
                    if (v != 0) {
                        fprintf(stderr, "  +0x%03x: 0x%016lx %s%s%s\n", off, (unsigned long)v,
                                is_heap_ptr(v) ? "(heap)" : is_any_ptr(v) ? "(ptr)" : "",
                                off == 0xA8 ? " <-- OwningWorld?" : "",
                                off == OFF_ULEVEL_ACTOR_CLUSTER ? " <-- ActorCluster" : "");
                    }
                }
            }

            uint64_t levels_data = read_ptr(uworld + OFF_UWORLD_LEVELS);
            int32_t levels_count = read_i32(uworld + OFF_UWORLD_LEVELS + 8);
            int32_t levels_max = read_i32(uworld + OFF_UWORLD_LEVELS + 12);
            fprintf(stderr, "\n[*] UWorld levels: data=0x%lx count=%d max=%d\n",
                    (unsigned long)levels_data, levels_count, levels_max);
            if (is_any_ptr(levels_data) && levels_count > 0 && levels_count <= 2048 &&
                levels_max >= levels_count && levels_max <= 4096) {
                uint64_t level_ptrs[2048];
                if (read_mem(levels_data, level_ptrs, (size_t)levels_count * 8) ==
                    (ssize_t)((size_t)levels_count * 8)) {
                    for (int i = 0; i < levels_count; i++) {
                        uint64_t streamed = level_ptrs[i];
                        if (!is_any_ptr(streamed)) continue;
                        uint64_t owner = read_ptr(streamed + 0xA8);
                        uint64_t actors = read_ptr(streamed + 0x98);
                        int32_t count = read_i32(streamed + 0xA0);
                        int32_t max = read_i32(streamed + 0xA4);
                        if (owner == uworld && is_any_ptr(actors) && count > 0 &&
                            count <= 50000 && max >= count && max <= 100000) {
                            fprintf(stderr, "  level[%d]=0x%lx actors=%d max=%d%s\n",
                                    i, (unsigned long)streamed, count, max,
                                    streamed == level ? " persistent" : "");
                        }
                    }
                }
            }

            /* Bounded live-layout probe: compare nearby actors to the local
             * pawn without relying on stale FName/GNames offsets. */
            uint64_t gi = read_ptr(uworld + 0x190);
            uint64_t lp_arr = is_any_ptr(gi) ? read_ptr(gi + 0x38) : 0;
            uint64_t lp0 = is_any_ptr(lp_arr) ? read_ptr(lp_arr) : 0;
            uint64_t pc = is_any_ptr(lp0) ? read_ptr(lp0 + 0x30) : 0;
            uint64_t pawn = is_any_ptr(pc) ? read_ptr(pc + 0x3A0) : 0;
            uint64_t pawn_class = is_any_ptr(pawn) ? read_ptr(pawn + OFF_UOBJECT_CLASS) : 0;
            uint64_t pawn_root = is_any_ptr(pawn) ? read_ptr(pawn + OFF_AACTOR_ROOT_COMPONENT) : 0;
            float self_x = is_any_ptr(pawn_root) ? read_float(pawn_root + OFF_USCENE_RELATIVE_LOCATION) : 0;
            float self_y = is_any_ptr(pawn_root) ? read_float(pawn_root + OFF_USCENE_RELATIVE_LOCATION + 4) : 0;
            float self_z = is_any_ptr(pawn_root) ? read_float(pawn_root + OFF_USCENE_RELATIVE_LOCATION + 8) : 0;
            fprintf(stderr, "\n[*] Local pc=0x%lx pawn=0x%lx class=0x%lx pos=(%.1f,%.1f,%.1f)\n",
                    (unsigned long)pc, (unsigned long)pawn, (unsigned long)pawn_class,
                    self_x, self_y, self_z);
            if (is_any_ptr(pawn_class)) {
                fprintf(stderr, "[*] Local class super chain:\n");
                uint64_t chain = pawn_class;
                for (int depth = 0; depth < 12 && is_any_ptr(chain); depth++) {
                    fprintf(stderr, "  super[%d] = 0x%lx\n", depth, (unsigned long)chain);
                    uint64_t next = read_ptr(chain + 0x40);
                    if (next == chain) break;
                    chain = next;
                }
                fprintf(stderr, "[*] Local class pointer fields:\n");
                for (int off = 0; off <= 0x100; off += 8) {
                    uint64_t v = read_ptr(pawn_class + off);
                    if (is_any_ptr(v))
                        fprintf(stderr, "  class+0x%02x = 0x%lx\n", off, (unsigned long)v);
                }
            }
            uint64_t actor_data = read_ptr(level + 0x98);
            int32_t actor_count = read_i32(level + 0xA0);
            if (is_any_ptr(actor_data) && actor_count > 0 && actor_count <= 50000) {
                int probe_count = actor_count < 2000 ? actor_count : 2000;
                uint64_t actor_ptrs[2000];
                if (read_mem(actor_data, actor_ptrs, (size_t)probe_count * 8) ==
                    (ssize_t)((size_t)probe_count * 8)) {
                    fprintf(stderr, "[*] Nearby persistent actors (<=1500m horizontal, <=300m vertical):\n");
                    for (int i = 0; i < probe_count; i++) {
                        uint64_t actor = actor_ptrs[i];
                        if (!is_any_ptr(actor)) continue;
                        uint64_t root = read_ptr(actor + OFF_AACTOR_ROOT_COMPONENT);
                        if (!is_any_ptr(root)) continue;
                        float x = read_float(root + OFF_USCENE_RELATIVE_LOCATION);
                        float y = read_float(root + OFF_USCENE_RELATIVE_LOCATION + 4);
                        float z = read_float(root + OFF_USCENE_RELATIVE_LOCATION + 8);
                        if (x != x || y != y || z != z) continue;
                        float dx = (x - self_x) / 100.0f;
                        float dy = (y - self_y) / 100.0f;
                        float dz = (z - self_z) / 100.0f;
                        if (sqrtf(dx * dx + dy * dy) > 1500.0f || fabsf(dz) > 300.0f) continue;
                        uint64_t cls = read_ptr(actor + OFF_UOBJECT_CLASS);
                        uint64_t ctrl = read_ptr(actor + OFF_APAWN_CONTROLLER);
                        uint64_t state = read_ptr(actor + OFF_APAWN_PLAYER_STATE);
                        fprintf(stderr,
                                "  [%d] actor=0x%lx class=0x%lx ctrl=0x%lx state=0x%lx "
                                "pos=(%.1f,%.1f,%.1f) rel=(%.1f,%.1f,%.1f)%s\n",
                                i, (unsigned long)actor, (unsigned long)cls,
                                (unsigned long)ctrl, (unsigned long)state,
                                x, y, z, dx, dy, dz, actor == pawn ? " SELF" : "");
                    }
                }
            }
            if (is_any_ptr(levels_data) && levels_count > 0 && levels_count <= 2048 &&
                is_any_ptr(pawn_class)) {
                uint64_t level_ptrs[2048];
                if (read_mem(levels_data, level_ptrs, (size_t)levels_count * 8) ==
                    (ssize_t)((size_t)levels_count * 8)) {
                    fprintf(stderr, "[*] Character pawns across streamed levels:\n");
                    for (int li = 0; li < levels_count; li++) {
                        uint64_t streamed = level_ptrs[li];
                        if (!is_any_ptr(streamed) || read_ptr(streamed + 0xA8) != uworld) continue;
                        uint64_t data = read_ptr(streamed + 0x98);
                        int32_t count = read_i32(streamed + 0xA0);
                        if (!is_any_ptr(data) || count <= 0 || count > 2000) continue;
                        uint64_t ptrs[2000];
                        if (read_mem(data, ptrs, (size_t)count * 8) != (ssize_t)((size_t)count * 8)) continue;
                        for (int ai = 0; ai < count; ai++) {
                            uint64_t actor = ptrs[ai];
                            uint64_t cls = is_any_ptr(actor) ? read_ptr(actor + OFF_UOBJECT_CLASS) : 0;
                            if (!is_character_class(cls, pawn_class)) continue;
                            uint64_t state = read_ptr(actor + OFF_APAWN_PLAYER_STATE);
                            uint64_t root = read_ptr(actor + OFF_AACTOR_ROOT_COMPONENT);
                            if (!is_any_ptr(root)) continue;
                            float x = read_float(root + OFF_USCENE_RELATIVE_LOCATION);
                            float y = read_float(root + OFF_USCENE_RELATIVE_LOCATION + 4);
                            float z = read_float(root + OFF_USCENE_RELATIVE_LOCATION + 8);
                            if (x != x || y != y || z != z || (x == 0 && y == 0 && z == 0)) continue;
                            char pname[64] = {0};
                            if (is_game_heap_ptr(state)) {
                                read_fstring(state + OFF_PLAYER_STATE_NAME, pname, sizeof(pname));
                                if (!pname[0]) read_fstring(state + OFF_PLAYER_STATE_NAME_PRIVATE, pname, sizeof(pname));
                            }
                            fprintf(stderr,
                                    "  level=%d actor=0x%lx class=0x%lx state=0x%lx "
                                    "pos=(%.1f,%.1f,%.1f) name=%s%s\n",
                                    li, (unsigned long)actor, (unsigned long)cls,
                                    (unsigned long)state, x, y, z,
                                    pname[0] ? pname : "(none)", actor == pawn ? " SELF" : "");
                        }
                    }
                }
            }
        }
        if (mem_fd >= 0) close(mem_fd);
        free(read_buf);
        return 0;
    }

    if (mode_scan) {
        scan_for_gworld();
        if (num_candidates == 0) {
            fprintf(stderr, "[-] No GWorld candidates found\n");
        } else {
            int best_idx = 0;
            for (int i = 1; i < num_candidates; i++) {
                if (candidates[i].score > candidates[best_idx].score ||
                    (candidates[i].score == candidates[best_idx].score &&
                     candidates[i].actor_count > candidates[best_idx].actor_count))
                    best_idx = i;
            }

            GWorldCandidate *best = &candidates[best_idx];
            fprintf(stderr, "\n[+] BEST: GWorld@0x%lx UWorld=0x%lx actors=%d score=%d%%\n",
                   (unsigned long)best->gworld_addr, (unsigned long)best->uworld,
                   best->actor_count, best->score);

            probe_positions(best);

            /* Print run command */
            fprintf(stderr, "\n[*] To run continuously:\n");
            fprintf(stderr, "  ./memscan run vmreadv gworld=%lx actorsrc=%d\n",
                    (unsigned long)best->gworld_addr, best->actor_source);
            fprintf(stderr, "  ./memscan run vmreadv gworld=%lx actorsrc=%d ctw\n",
                    (unsigned long)best->gworld_addr, best->actor_source);
        }

        if (mem_fd >= 0) close(mem_fd);
        free(read_buf);
        return 0;
    }

    if (mode_run) {
        if (use_gworld == 0) {
            if (load_world_cache(game_pid, &use_gworld, &use_actor_src)) {
                fprintf(stderr, "[+] Reusing verified world cache GWorld@0x%lx actorsrc=%d\n",
                        (unsigned long)use_gworld, use_actor_src);
            } else {
                fprintf(stderr, "[*] No valid world cache for PID %d; performing one bounded discovery...\n",
                        game_pid);
                scan_for_gworld();
                keep_gameplay_candidates_only();
            }
            if (use_gworld != 0) {
                /* Cache was accepted above. */
            } else if (num_candidates == 0) {
                /* A launcher-started scanner normally attaches while the user
                 * is still in the menu. Keep the early authorized map stream
                 * and wait for gameplay instead of exiting after startup. */
                while (running && num_candidates == 0) {
                    fprintf(stderr, "[*] No active gameplay world; waiting for a match...\n");
                    diagnostic_status = "waiting for gameplay";
                    post_diagnostics();
                    sleep(15);
                    parse_maps();
                    scan_for_gworld();
                    keep_gameplay_candidates_only();
                }
                if (!running) {
                    if (mem_fd >= 0) close(mem_fd);
                    free(read_buf);
                    return 0;
                }
            } else {
                /* handled below */
            }

            if (use_gworld == 0 && num_candidates > 0) {
                int best_idx = 0;
                for (int i = 1; i < num_candidates; i++)
                    if (candidates[i].score > candidates[best_idx].score ||
                        (candidates[i].score == candidates[best_idx].score &&
                         candidates[i].plausible_players > candidates[best_idx].plausible_players))
                        best_idx = i;
                use_gworld = candidates[best_idx].gworld_addr;
                use_actor_src = candidates[best_idx].actor_source;
                save_world_cache(game_pid, use_gworld, use_actor_src);
                fprintf(stderr, "[+] Auto-selected and cached GWorld@0x%lx actorsrc=%d players=%d\n",
                        (unsigned long)use_gworld, use_actor_src,
                        candidates[best_idx].plausible_players);
            }
        }

        fprintf(stderr, "[+] Running: gworld=0x%lx actorsrc=%d root=+0x%x pos=+0x%x ctw=%d\n",
                (unsigned long)use_gworld, use_actor_src, use_root_off, use_pos_off, use_ctw);

        int consecutive_failures = 0;
        int zero_entity_frames = 0;
        int last_reported_count = -1;
        unsigned report_tick = 0;
        unsigned pid_tick = 0;
        int game_missing = 0;
        while (running) {
            if ((pid_tick++ % 30) == 0) {
                int active_pid = find_game_pid();
                if (active_pid > 0 && active_pid != game_pid) {
                    fprintf(stderr, "\n[+] Game PID changed %d -> %d; reattaching\n",
                            game_pid, active_pid);
                    num_entities = 0;
                    self_entity_id[0] = 0;
                    diagnostics_game_unavailable();
                    post_to_radar();
                    if (maps_file) { fclose(maps_file); maps_file = NULL; }
                    if (mem_fd >= 0) { close(mem_fd); mem_fd = -1; }
                    execv(argv[0], argv);
                    fprintf(stderr, "[-] Scanner reattach exec failed: %s\n", strerror(errno));
                    break;
                }
                if (active_pid <= 0 && !game_missing) {
                    /* Immediately remove the previous match from the Android
                     * overlay while the game is closed or restarting. */
                    int saved_count = num_entities;
                    char saved_self[sizeof(self_entity_id)];
                    memcpy(saved_self, self_entity_id, sizeof(saved_self));
                    num_entities = 0;
                    self_entity_id[0] = 0;
                    diagnostics_game_unavailable();
                    post_to_radar();
                    num_entities = saved_count;
                    memcpy(self_entity_id, saved_self, sizeof(self_entity_id));
                    game_missing = 1;
                } else if (active_pid == game_pid) {
                    game_missing = 0;
                }
            }
            int n = read_entities_sdk(use_gworld, use_actor_src,
                                      use_root_off, use_pos_off, use_ctw);
            if (n >= 0) {
                int publish_count = self_verified ? n : 0;
                if (publish_count > 0) {
                    post_to_radar();
                    zero_entity_frames = 0;
                } else {
                    /* Empty snapshots clear stale overlay state in menus,
                     * loading screens, and between matches. */
                    int saved_count = num_entities;
                    char saved_self[sizeof(self_entity_id)];
                    memcpy(saved_self, self_entity_id, sizeof(saved_self));
                    num_entities = 0;
                    self_entity_id[0] = 0;
                    post_to_radar();
                    num_entities = saved_count;
                    memcpy(self_entity_id, saved_self, sizeof(self_entity_id));
                    /* A valid but unverified lobby world does not need a costly
                     * global rescan; a truly empty/broken world still does. */
                    if (n > 0) zero_entity_frames = 0;
                    else zero_entity_frames++;
                }
                if (publish_count != last_reported_count || (++report_tick % 150) == 0) {
                    fprintf(stderr, "\r[+] Entities: %d%s    ", publish_count,
                            n > 0 && !self_verified ? " (unverified world)" : "");
                    last_reported_count = publish_count;
                }
                consecutive_failures = 0;
            } else {
                consecutive_failures++;
                if (consecutive_failures == 1 || consecutive_failures % 30 == 0)
                    post_diagnostics();
            }
            int need_rescan = consecutive_failures > 10 ||
                              zero_entity_frames > 150; /* ~5s of 0 entities */
            if (need_rescan) {
                int active_pid = find_game_pid();
                if (active_pid > 0 && active_pid != game_pid) {
                    fprintf(stderr, "\n[+] Game PID changed %d -> %d before rescan; reattaching\n",
                            game_pid, active_pid);
                    num_entities = 0;
                    self_entity_id[0] = 0;
                    diagnostics_game_unavailable();
                    post_to_radar();
                    if (maps_file) { fclose(maps_file); maps_file = NULL; }
                    if (mem_fd >= 0) { close(mem_fd); mem_fd = -1; }
                    execv(argv[0], argv);
                    fprintf(stderr, "[-] Scanner reattach exec failed: %s\n", strerror(errno));
                    break;
                }
                fprintf(stderr, "\n[!] Re-scanning (failures=%d zero=%d)...\n",
                        consecutive_failures, zero_entity_frames);
                parse_maps();
                scan_for_gworld();
                keep_gameplay_candidates_only();
                if (num_candidates > 0) {
                    int best_idx = 0;
                    for (int i = 1; i < num_candidates; i++)
                        if (candidates[i].score > candidates[best_idx].score ||
                            (candidates[i].score == candidates[best_idx].score &&
                             candidates[i].plausible_players > candidates[best_idx].plausible_players))
                            best_idx = i;
                    use_gworld = candidates[best_idx].gworld_addr;
                    use_actor_src = candidates[best_idx].actor_source;
                    save_world_cache(game_pid, use_gworld, use_actor_src);
                    fprintf(stderr, "[+] Re-selected GWorld@0x%lx actorsrc=%d players=%d\n",
                            (unsigned long)use_gworld, use_actor_src,
                            candidates[best_idx].plausible_players);
                } else {
                    fprintf(stderr, "[!] No world found, will retry...\n");
                    /* Menus/loading screens have no gameplay candidate. Avoid
                     * hammering the process with full scans while waiting. */
                    sleep(15);
                }
                consecutive_failures = 0;
                zero_entity_frames = 0;
            }
            usleep(33000); /* ~30 Hz data feed */
        }
        fprintf(stderr, "\n[*] Stopped\n");
        if (mem_fd >= 0) close(mem_fd);
        free(read_buf);
        return 0;
    }

    if (mem_fd >= 0) close(mem_fd);
    free(read_buf);
    return 0;
}
