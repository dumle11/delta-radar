/*
 * Delta Force memory scanner - finds UE4 structures via process_vm_readv
 * SDK dump offsets from github.com/eikarna/deltaforce-dump (Garena v1.203)
 *
 * Reproducible ARM64/API-31 build: scanner/tools/build_scanner.py --ndk PATH
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

#ifndef DELTA_SOURCE_REVISION
#define DELTA_SOURCE_REVISION "unrecorded"
#endif
#ifndef DELTA_SOURCE_SHA256
#define DELTA_SOURCE_SHA256 "unrecorded"
#endif

static volatile sig_atomic_t running = 1;
static const char *session_file = NULL;
static pid_t session_owner_pid = 0;

/* A cancelled app session cannot turn a delayed root command into a live read.
 * This checks only our app's lease file; it does not alter the game process. */
static int session_active(void) {
    if (!running) return 0;
    if (!session_file) return 1;
    static long long checked_at = -1;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long now = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (checked_at >= 0 && now - checked_at < 50) return running;
    checked_at = now;
    if (session_owner_pid <= 1 || kill(session_owner_pid, 0) != 0) {
        running = 0;
        return 0;
    }
    FILE *f = fopen(session_file, "r");
    char lease[16] = {0};
    int valid = f && fgets(lease, sizeof(lease), f) && strcmp(lease, "active\n") == 0;
    if (f) fclose(f);
    if (!valid) running = 0;
    return running;
}

#define GAME_PKG "com.proxima.dfm"
#define RADAR_SOCKET_NAME "delta_radar_feed"
#define MAX_ACTORS 512
#define READ_BUF_SIZE (4 * 1024 * 1024)
#define MAX_REGIONS 16384

/* Proxima v1.202.37117.5601: matching CppSDK/SDK layouts. */
#define OFF_UWORLD_PERSISTENT_LEVEL   0xF8
#define OFF_UWORLD_GAME_STATE         0x140
#define OFF_UWORLD_LEVELS             0x158
#define OFF_ULEVEL_ACTOR_CLUSTER      0xC8
#define OFF_ACTOR_CONTAINER_ACTORS    0x28
#define OFF_AACTOR_ROOT_COMPONENT     0x180
#define OFF_AACTOR_CHILDREN           0x170
#define OFF_USCENE_RELATIVE_LOCATION  0x168
#define OFF_USCENE_COMPONENT_TO_WORLD 0x210
#define OFF_TRANSFORM_TRANSLATION      0x10
#define OFF_USCENE_WORLD_LOCATION      (OFF_USCENE_COMPONENT_TO_WORLD + OFF_TRANSFORM_TRANSLATION)
#define OFF_PROXIMA_ROOT_WORLD_LOCATION OFF_USCENE_WORLD_LOCATION
#define OFF_UOBJECT_CLASS             0x8
#define OFF_UOBJECT_NAME              0x1C
#define OFF_UOBJECT_OUTER             0x10
#define OFF_GNAMES_FROM_UE4_BASE      0x20744AC0
/* The canonical FNamePool page above is mapped write-only in this regional
 * build, so process_vm_readv cannot inspect it.  This read/write UE4 alias
 * references the same pool and avoids a 56 MB discovery scan on every start. */
#define OFF_GNAMES_READABLE_ALIAS     0x1BEE1F60
#define OFF_GUOBJECTARRAY_FROM_BASE   0x2076B8F8
#define OFF_GWORLD_FROM_UE4_BASE      0x20AA87A0
#define OFF_FNAME_POOL_BLOCKS         0x38

/* Proxima v1.202: actor arrays move within ULevel between matches.
 * Scan a range of the level object for TArray patterns instead. */
#define ULEVEL_SCAN_START 0x98
#define ULEVEL_SCAN_END   0x300
#define ULEVEL_SCAN_STEP  8

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
static int use_vm_readv = 1;
static int compatibility_scan = 0;
static FILE *maps_file = NULL;

/* Observational discovery diagnostics: fixed storage, no additional game reads.
 * Counters count events, not unique objects; pointer aliases may repeat. */
enum DiagCheck {
    DC_WORLD_OUTSIDE_MODULE, DC_LEVEL_POINTER, DC_LEVEL_OUTSIDE_MODULE,
    DC_CLUSTER_POINTER, DC_CLUSTER_OUTSIDE_MODULE,
    DC_COUNT_MIN, DC_COUNT_MAX, DC_CAPACITY_MIN, DC_CAPACITY_MAX, DC_ARRAY_POINTER,
    DC_ARRAY_READ, DC_SCORE_IMPROVES, DC_THREE_POINTERS, DC_BEST_LIST,
    DC_SAMPLE_NONEMPTY, DC_SAMPLE_READ, DC_SAMPLE_ACTOR_POINTER,
    DC_LOCAL_CLASS, DC_CHARACTER_CLASS, DC_ROOT_POINTER,
    DC_X_FINITE, DC_Y_FINITE, DC_Z_FINITE, DC_X_BOUND, DC_Y_BOUND, DC_Z_BOUND,
    DC_NONZERO_POSITION, DC_BACKREF_MATCH, DC_PC_GAME_HEAP, DC_PAWN_GAME_HEAP, DC_HAS_PLAUSIBLE, DC_SCORE_THRESHOLD, DC_CHECK_COUNT
};
static const char *const diag_check_names[] = {
    "world_outside_module", "level_pointer_valid", "level_outside_module",
    "cluster_pointer_valid", "cluster_outside_module",
    "count_ge_5", "count_le_50000", "capacity_ge_count", "capacity_le_100000", "array_pointer_valid",
    "array_read_complete", "score_improves_best", "valid_pointer_count_ge_3", "best_list_score_gt_0",
    "sample_nonempty", "sample_read_complete", "sample_actor_pointer_valid",
    "local_class_pointer_valid", "actor_matches_character_class", "root_pointer_valid",
    "x_not_nan", "y_not_nan", "z_not_nan", "abs_x_le_10000000", "abs_y_le_10000000", "abs_z_le_10000000",
    "position_nonzero", "owning_world_matches", "controller_in_game_heap", "pawn_in_game_heap", "plausible_players_gt_0", "score_ge_70"
};
enum DiagStage { DS_BULK, DS_LEVEL, DS_BACKREF, DS_CLUSTER, DS_ARRAY_FIELDS,
    DS_ARRAY_CONTENTS, DS_LOCAL_CHAIN, DS_PLAYER_SAMPLE, DS_CLASS_ROOT, DS_POSITION, DS_COUNT };
static const char *const diag_stage_names[] = {
    "scan_mapping", "persistent_level", "owning_world", "actor_cluster", "array_fields",
    "array_contents", "local_player_chain", "player_sample", "class_and_root", "position"
};
enum DiagMapReason { DM_SELECTED, DM_NOT_RW, DM_EXECUTABLE, DM_NAMED_OTHER,
    DM_ANON_OUTSIDE, DM_SIZE, DM_COUNT };
static const char *const diag_map_names[] = {
    "selected", "not_read_write", "executable", "named_not_ue4", "anonymous_outside_neighbourhood", "size_outside_limits"
};
typedef struct { uint64_t calls, complete, partial, failed, requested, returned; } DiagReads;
typedef struct {
    int region, reason;
    uint64_t start, end, read_bytes, slots;
    char perms[8], label[48];
} DiagMapping;
typedef struct {
    uint64_t key, ordinal, failed_checks;
    int score, raw_score, reached, read_failures, partial_reads;
    int path_score[5], path_valid[5], path_checked[5];
    uint64_t path_failed[5];
} DiagCandidate;
typedef struct {
    int active, read_stage, path, mapping_detail;
    uint64_t pass, started_ms, slots, potential, validation_calls;
    uint64_t level_valid, lists_valid, lists_read, lists_selected, worlds_with_list;
    uint64_t sample_actors, plausible_players, backref_match, backref_mismatch;
    uint64_t local_pointer_valid[6], score_ge70, score_ge70_with_players;
    uint64_t passed[DC_CHECK_COUNT], rejected[DC_CHECK_COUNT], path_rejected[5][DC_CHECK_COUNT];
    uint64_t map_count[DM_COUNT], map_bytes[DM_COUNT], maps_visited;
    uint64_t errno_counts[256], errno_other;
    DiagReads reads[DS_COUNT];
    DiagMapping mappings[56];
    int mapping_count, selected_details, excluded_details[DM_COUNT];
    int max_score, max_raw_score;
    DiagCandidate current, top[3];
    int top_count;
} DiscoveryDiagnostics;
static DiscoveryDiagnostics discovery_diag;
static uint64_t discovery_passes, diag_last_emit_ms, diag_suppressed_passes;
static int diag_maps_rows, diag_maps_truncated, diag_maps_malformed, diag_maps_errno;
static uint64_t diag_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
static void diag_begin(void) {
    memset(&discovery_diag, 0, sizeof(discovery_diag));
    discovery_diag.active = 1;
    discovery_diag.path = -1; discovery_diag.mapping_detail = -1;
    discovery_diag.pass = ++discovery_passes;
    discovery_diag.started_ms = diag_now_ms();
}
static void diag_stage(int stage) { if (discovery_diag.active) discovery_diag.read_stage = stage; }
static void diag_path(int path) { if (discovery_diag.active) discovery_diag.path = path; }
static int diag_check(int ok, int check) {
    if (ok && discovery_diag.active) discovery_diag.passed[check]++;
    if (!ok && discovery_diag.active) {
        discovery_diag.rejected[check]++;
        discovery_diag.current.failed_checks |= UINT64_C(1) << check;
        int path = discovery_diag.path;
        if (path >= 0 && path < 5) {
            discovery_diag.path_rejected[path][check]++;
            discovery_diag.current.path_failed[path] |= UINT64_C(1) << check;
        }
    }
    return ok;
}
#define D_CHECK(check, expression) diag_check((expression), (check))
#define D_COUNT(field) do { if (discovery_diag.active) discovery_diag.field++; } while (0)
static void diag_read(size_t requested, ssize_t result, int error) {
    if (!discovery_diag.active) return;
    DiagReads *r = &discovery_diag.reads[discovery_diag.read_stage];
    r->calls++; r->requested += requested;
    if (result >= 0) r->returned += (uint64_t)result;
    if (result == (ssize_t)requested) r->complete++;
    else if (result >= 0) { r->partial++; discovery_diag.current.partial_reads++; }
    else {
        r->failed++; discovery_diag.current.read_failures++;
        if (error > 0 && error < 256) discovery_diag.errno_counts[error]++;
        else discovery_diag.errno_other++;
    }
}
static void diag_mapping(int region, int reason) {
    if (!discovery_diag.active) return;
    MemRegion *r = &regions[region];
    discovery_diag.maps_visited++;
    discovery_diag.map_count[reason]++;
    discovery_diag.map_bytes[reason] += r->end - r->start;
    discovery_diag.mapping_detail = -1;
    /* 32 selected mappings + 4 samples for each exclusion reason, max 52. */
    if ((reason == DM_SELECTED && discovery_diag.selected_details >= 32) ||
        (reason != DM_SELECTED && discovery_diag.excluded_details[reason] >= 4)) return;
    int slot = discovery_diag.mapping_count++;
    DiagMapping *m = &discovery_diag.mappings[slot];
    m->region = region; m->reason = reason; m->start = r->start; m->end = r->end;
    memcpy(m->perms, r->perms, sizeof(m->perms));
    const char *label = strrchr(r->name, '/');
    label = label ? label + 1 : r->name;
    if (!*label) label = "anonymous";
    /* Basename only, printable conservative alphabet; no full app paths. */
    for (int i = 0; i < 47 && label[i]; i++) {
        char c = label[i];
        m->label[i] = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' || c == '[' || c == ']' ? c : '_';
    }
    if (reason == DM_SELECTED) {
        discovery_diag.selected_details++; discovery_diag.mapping_detail = slot;
    } else discovery_diag.excluded_details[reason]++;
}
static void diag_candidate_begin(uint64_t key) {
    if (!discovery_diag.active) return;
    memset(&discovery_diag.current, 0, sizeof(discovery_diag.current));
    discovery_diag.current.key = key;
    discovery_diag.current.ordinal = ++discovery_diag.validation_calls;
    discovery_diag.path = -1;
}
static void diag_reached(int stage) {
    if (discovery_diag.active && stage > discovery_diag.current.reached)
        discovery_diag.current.reached = stage;
}
static void diag_path_score(int score, int valid, int checked) {
    if (!discovery_diag.active) return;
    int path = discovery_diag.path;
    if (path >= 0 && path < 5) {
        discovery_diag.current.path_score[path] = score;
        discovery_diag.current.path_valid[path] = valid;
        discovery_diag.current.path_checked[path] = checked;
    }
    if (score > discovery_diag.current.raw_score) discovery_diag.current.raw_score = score;
    if (score > discovery_diag.max_raw_score) discovery_diag.max_raw_score = score;
}
static int diag_better(const DiagCandidate *a, const DiagCandidate *b) {
    if (a->score != b->score) return a->score > b->score;
    if (a->raw_score != b->raw_score) return a->raw_score > b->raw_score;
    return a->reached > b->reached;
}
static void diag_candidate_finish(int score, int players) {
    if (!discovery_diag.active) return;
    DiagCandidate *c = &discovery_diag.current;
    c->score = score;
    if (score > discovery_diag.max_score) discovery_diag.max_score = score;
    if (score >= 70) { discovery_diag.score_ge70++; if (players > 0) discovery_diag.score_ge70_with_players++; }
    /* De-duplicate only the bounded diagnostic sample; never the search. */
    for (int i = 0; i < discovery_diag.top_count; i++) {
        if (discovery_diag.top[i].key == c->key) {
            if (!diag_better(c, &discovery_diag.top[i])) return;
            discovery_diag.top[i] = *c;
            while (i > 0 && diag_better(&discovery_diag.top[i], &discovery_diag.top[i-1])) {
                DiagCandidate swap = discovery_diag.top[i-1];
                discovery_diag.top[i-1] = discovery_diag.top[i]; discovery_diag.top[i] = swap; i--;
            }
            return;
        }
    }
    int index = discovery_diag.top_count;
    if (index < 3) discovery_diag.top_count++;
    else { index = 2; if (!diag_better(c, &discovery_diag.top[index])) return; }
    discovery_diag.top[index] = *c;
    while (index > 0 && diag_better(&discovery_diag.top[index], &discovery_diag.top[index-1])) {
        DiagCandidate swap = discovery_diag.top[index-1];
        discovery_diag.top[index-1] = discovery_diag.top[index]; discovery_diag.top[index] = swap; index--;
    }
}
static void diag_print_failed(FILE *f, uint64_t mask) {
    int comma = 0;
    fputc('[', f);
    for (int i = 0; i < DC_CHECK_COUNT; i++) if (mask & (UINT64_C(1) << i)) {
        fprintf(f, "%s\"%s\"", comma++ ? "," : "", diag_check_names[i]);
    }
    fputc(']', f);
}
static int diag_report(FILE *f);

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
    if (!maps_file) { diag_maps_errno = errno; return -1; }
    diag_maps_errno = 0; diag_maps_rows = 0; diag_maps_truncated = 0; diag_maps_malformed = 0;
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
            { diag_maps_malformed++; continue; }
        diag_maps_rows++;
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
        } else diag_maps_truncated++;
    }
    return 0;
}

static int parse_maps_with_retry(void) {
    /* Cold starts after Android's debugger gate can take several seconds to
     * map libUE4 on older devices. Keep the early-authorized proc fd alive. */
    for (int attempt = 0; attempt < 600 && session_active(); attempt++) {
        if (parse_maps() == 0 && num_regions > 0 && ue4_base != 0) return 0;
        usleep(100000);
    }
    return -1;
}

static ssize_t read_mem(uint64_t addr, void *buf, size_t len) {
    ssize_t result;
    if (!session_active()) { errno = ECANCELED; result = -1; }
    else if (use_vm_readv) {
        struct iovec local = { .iov_base = buf, .iov_len = len };
        struct iovec remote = { .iov_base = (void *)addr, .iov_len = len };
        result = syscall(__NR_process_vm_readv, game_pid, &local, 1, &remote, 1, 0);
    } else if (mem_fd < 0) result = -1;
    else if (lseek64(mem_fd, (off64_t)addr, SEEK_SET) == (off64_t)-1) result = -1;
    else result = read(mem_fd, buf, len);
    int saved_errno = errno;
    diag_read(len, result, !use_vm_readv && mem_fd < 0 && running ? -1 : saved_errno);
    errno = saved_errno;
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
    /* From remote master 4e844f7: accept ASLR-moved object heaps while excluding
     * the engine image. A previous process's fixed heap range is not a type. */
    return is_any_ptr(p) && (p & 7) == 0 && !is_in_ue4(p);
}

#ifdef DELTA_WITH_DECODER
extern int delta_decode_position(int pid, uint64_t base, uint64_t root, float out[3]);
extern int delta_decode_character_bounds(int pid, uint64_t base, uint64_t root, float out[3]);
#endif
/* Remote master's enemy path reads +0x148. The matching SDK's bounds accessor
 * confirms this is FEncBoxSphereBounds::Origin, not the loot transform. */
static int read_character_position(uint64_t root, float out[3]) {
    unsigned char origin[16];
    if (read_mem(root + 0x148, origin, sizeof(origin)) != sizeof(origin)) return 0;
    uint16_t index;
    memcpy(&index, origin + 12, sizeof(index));
    if (index != UINT16_MAX) {
#ifdef DELTA_WITH_DECODER
        return delta_decode_character_bounds(game_pid, ue4_base, root, out);
#else
        return 0;
#endif
    }
    memcpy(out, origin, 3 * sizeof(float));
    for (int i=0;i<3;i++) if (!isfinite(out[i]) || fabsf(out[i]) > 10000000) return 0;
    return out[0] != 0 || out[1] != 0 || out[2] != 0;
}
static int read_component_position(uint64_t root, int pos_off, float out[3]) {
    if (pos_off == OFF_USCENE_WORLD_LOCATION) {
        float transform[12];
        if (read_mem(root + OFF_USCENE_COMPONENT_TO_WORLD, transform, sizeof(transform)) != sizeof(transform)) return 0;
        const uint8_t *handler = ((const uint8_t*)transform)+0x28;
        if (handler[2]) {
#ifdef DELTA_WITH_DECODER
            return delta_decode_position(game_pid, ue4_base, root, out);
#else
            return 0;
#endif
        }
        float norm = 0;
        for (int i=0;i<4;i++) { if (!isfinite(transform[i])) return 0; norm += transform[i]*transform[i]; }
        if (norm < 0.5f || norm > 1.5f) return 0;
        memcpy(out, transform+4, 3*sizeof(float));
    } else if (read_mem(root + pos_off, out, 3*sizeof(float)) != 3*sizeof(float)) return 0;
    for (int i=0;i<3;i++) if (!isfinite(out[i]) || fabsf(out[i]) > 10000000) return 0;
    return out[0] != 0 || out[1] != 0 || out[2] != 0;
}

#define OFF_APAWN_CONTROLLER 0x3a8
#define OFF_APAWN_PLAYER_STATE 0x390
#define OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN 0x3F0
#define OFF_PLAYER_STATE_NAME 0x388
#define OFF_PLAYER_STATE_NAME_PRIVATE 0x470
#define OFF_PLAYER_STATE_BOT_FLAGS 0x39e
#define OFF_PLAYER_STATE_PAWN_PRIVATE 0x3F8
#define OFF_GP_PLAYER_STATE_TEAM_ID 0x658
#define OFF_CHARACTER_HEALTH_COMPONENT 0x10B8
#define OFF_HEALTH_COMPONENT_SET 0x280
#define OFF_HEALTH_SET_CURRENT 0x3C
#define OFF_HEALTH_SET_MAX 0x54
#define OFF_HEALTH_SET_IS_ALIVE 0x134
#define OFF_CHARACTER_LAST_IS_DEAD 0xE35
#define OFF_PLAYER_STATE_DEAD 0x4AC
#define OFF_CHARACTER_CAPSULE_HALF_HEIGHT 0xE68
#define OFF_CAPSULE_COMPONENT_HALF_HEIGHT 0x5C0

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

static int diag_report(FILE *f) {
    DiscoveryDiagnostics *d = &discovery_diag;
    d->active = 0; /* Reporting performs no reads and is never counted as discovery. */
    uint64_t now = diag_now_ms();
    if (diag_last_emit_ms && now - diag_last_emit_ms < 5000) { diag_suppressed_passes++; return 0; }
    diag_last_emit_ms = now;
    const char *outcome = !d->slots ? "no_pointer_slots_examined" : !d->validation_calls ?
        "no_potential_world_pointers" : !d->score_ge70 ? "examined_and_rejected" : "score_qualified_candidates";
    fprintf(f, "[discovery] {\"schema\":1,\"pass\":%llu,\"elapsedMs\":%llu,\"suppressedPasses\":%llu,\"outcome\":\"%s\",",
        (unsigned long long)d->pass, (unsigned long long)(now-d->started_ms),
        (unsigned long long)diag_suppressed_passes, outcome);
    fprintf(f, "\"maps\":{\"parsedRows\":%d,\"retained\":%d,\"truncated\":%d,\"malformedRows\":%d,\"parseErrno\":%d,\"visited\":%llu,\"notVisited\":%llu,\"groups\":{",
        diag_maps_rows, num_regions, diag_maps_truncated, diag_maps_malformed, diag_maps_errno,
        (unsigned long long)d->maps_visited, (unsigned long long)((uint64_t)num_regions-d->maps_visited));
    for (int i=0;i<DM_COUNT;i++) fprintf(f, "%s\"%s\":{\"mappings\":%llu,\"bytes\":%llu}", i?",":"",diag_map_names[i],
        (unsigned long long)d->map_count[i],(unsigned long long)d->map_bytes[i]);
    fprintf(f, "},\"detailLimit\":{\"selected\":32,\"perExclusion\":4},\"details\":[");
    for (int i=0;i<d->mapping_count;i++) {
        DiagMapping *m=&d->mappings[i];
        fprintf(f, "%s{\"region\":%d,\"start\":\"%llx\",\"end\":\"%llx\",\"perms\":\"%.4s\",\"label\":\"%s\",\"decision\":\"%s\",\"readBytes\":%llu,\"examinedSlots\":%llu}",
            i?",":"",m->region,(unsigned long long)m->start,(unsigned long long)m->end,m->perms,m->label,diag_map_names[m->reason],
            (unsigned long long)m->read_bytes,(unsigned long long)m->slots);
    }
    fprintf(f, "]},\"readsByStage\":{");
    for (int i=0;i<DS_COUNT;i++) {
        DiagReads *r=&d->reads[i];
        fprintf(f, "%s\"%s\":{\"calls\":%llu,\"complete\":%llu,\"partial\":%llu,\"failed\":%llu,\"requestedBytes\":%llu,\"returnedBytes\":%llu}",i?",":"",diag_stage_names[i],
            (unsigned long long)r->calls,(unsigned long long)r->complete,(unsigned long long)r->partial,
            (unsigned long long)r->failed,(unsigned long long)r->requested,(unsigned long long)r->returned);
    }
    fprintf(f,"},\"readErrors\":{"); int comma=0;
    for(int i=1;i<256;i++) if(d->errno_counts[i]) fprintf(f,"%s\"%d\":%llu",comma++?",":"",i,(unsigned long long)d->errno_counts[i]);
    fprintf(f,"},\"otherReadErrors\":%llu,\"stages\":{\"pointerSlots\":%llu,\"examinedBytes\":%llu,\"potentialPointers\":%llu,\"validationCalls\":%llu,\"validLevels\":%llu,\"validArrays\":%llu,\"arraysRead\":%llu,\"listsSelected\":%llu,\"worldsWithList\":%llu,\"sampleActors\":%llu,\"plausiblePlayers\":%llu,\"scoreGe70\":%llu,\"scoreGe70WithPlayers\":%llu,\"backrefMatch\":%llu,\"backrefMismatch\":%llu},",
        (unsigned long long)d->errno_other,(unsigned long long)d->slots,(unsigned long long)(d->slots*8),
        (unsigned long long)d->potential,(unsigned long long)d->validation_calls,(unsigned long long)d->level_valid,
        (unsigned long long)d->lists_valid,(unsigned long long)d->lists_read,(unsigned long long)d->lists_selected,
        (unsigned long long)d->worlds_with_list,(unsigned long long)d->sample_actors,(unsigned long long)d->plausible_players,
        (unsigned long long)d->score_ge70,(unsigned long long)d->score_ge70_with_players,
        (unsigned long long)d->backref_match,(unsigned long long)d->backref_mismatch);
    fprintf(f,"\"localChainPointerValid\":[");
    for(int i=0;i<6;i++) fprintf(f,"%s%llu",i?",":"",(unsigned long long)d->local_pointer_valid[i]);
    fprintf(f,"],\"failedChecks\":{");
    for(int i=0;i<DC_CHECK_COUNT;i++) fprintf(f,"%s\"%s\":%llu",i?",":"",diag_check_names[i],(unsigned long long)d->rejected[i]);
    fprintf(f,"},\"passedChecks\":{");
    for(int i=0;i<DC_CHECK_COUNT;i++) fprintf(f,"%s\"%s\":%llu",i?",":"",diag_check_names[i],(unsigned long long)d->passed[i]);
    fprintf(f,"},\"arrayPathFailedChecks\":[");
    for(int p=0;p<5;p++) {
        fprintf(f,"%s{\"path\":%d,\"failed\":{",p?",":"",p); int path_comma=0;
        for(int i=0;i<DC_CHECK_COUNT;i++) if(d->path_rejected[p][i]) fprintf(f,"%s\"%s\":%llu",path_comma++?",":"",diag_check_names[i],(unsigned long long)d->path_rejected[p][i]);
        fprintf(f,"}}");
    }
    fprintf(f,"],\"maxScore\":%d,\"maxRawArrayScore\":%d,\"candidateCapReached\":%s,\"highestCandidates\":[",d->max_score,d->max_raw_score,num_candidates>=64?"true":"false");
    for(int i=0;i<d->top_count;i++) {
        DiagCandidate *c=&d->top[i];
        fprintf(f,"%s{\"ordinal\":%llu,\"score\":%d,\"rawArrayScore\":%d,\"stage\":%d,\"failedReads\":%d,\"partialReads\":%d,\"failedChecks\":",i?",":"",(unsigned long long)c->ordinal,c->score,c->raw_score,c->reached,c->read_failures,c->partial_reads);
        diag_print_failed(f,c->failed_checks); fprintf(f,",\"paths\":[");
        for(int p=0;p<5;p++) {
            fprintf(f,"%s{\"path\":%d,\"score\":%d,\"valid\":%d,\"checked\":%d,\"failedChecks\":",p?",":"",p,c->path_score[p],c->path_valid[p],c->path_checked[p]);
            diag_print_failed(f,c->path_failed[p]); fprintf(f,"}");
        }
        fprintf(f,"]}");
    }
    fprintf(f,"]}\n");
    diag_suppressed_passes=0;
    return 1;
}

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
static uint64_t fname_block_cache[8192];
static int fname_entry_hdr_size = 2; /* bytes before the ASCII string in FNameEntry */

/* Try to validate a UWorld pointer using SDK dump offsets.
 * Path 1 (SDK dump): UWorld+0xF8 -> ULevel, ULevel+0xC8 -> ActorCluster, ActorCluster+0x28 -> TArray
 * Path 2 (standard UE4): UWorld+0xF8 -> ULevel, ULevel+0x98/0xA0 -> TArray directly (hidden in pad)
 */
static int try_validate_uworld(uint64_t uworld_ptr, GWorldCandidate *out) {
    if (!D_CHECK(DC_WORLD_OUTSIDE_MODULE, !is_in_ue4(uworld_ptr))) return 0;
    diag_reached(1);
    diag_stage(DS_LEVEL);

    uint64_t level = read_ptr(uworld_ptr + OFF_UWORLD_PERSISTENT_LEVEL);
    if (!D_CHECK(DC_LEVEL_POINTER, is_heap_ptr(level)) ||
        !D_CHECK(DC_LEVEL_OUTSIDE_MODULE, !is_in_ue4(level))) return 0;
    D_COUNT(level_valid); diag_reached(2);
    diag_stage(DS_BACKREF);

    /* Sanity: check ULevel->OwningWorld points back to uworld_ptr.
     * Lobby probe found backref at +0x1E0; SDK dump had +0xA8. Try both. */
    uint64_t owning_world = read_ptr(level + 0x1E0);
    int has_backref = (owning_world == uworld_ptr);
    if (!has_backref) {
        owning_world = read_ptr(level + 0xA8);
        has_backref = (owning_world == uworld_ptr);
    }
    (void)D_CHECK(DC_BACKREF_MATCH, has_backref);
    if (has_backref) D_COUNT(backref_match); else D_COUNT(backref_mismatch);

    int best_score = 0;

    /* Path 1: ActorCluster indirect */
    diag_path(0); diag_stage(DS_CLUSTER);
    uint64_t cluster = read_ptr(level + OFF_ULEVEL_ACTOR_CLUSTER);
    if (D_CHECK(DC_CLUSTER_POINTER, is_heap_ptr(cluster)) &&
        D_CHECK(DC_CLUSTER_OUTSIDE_MODULE, !is_in_ue4(cluster))) {
        diag_stage(DS_ARRAY_FIELDS);
        uint64_t actor_data = read_ptr(cluster + OFF_ACTOR_CONTAINER_ACTORS);
        int32_t count = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 8);
        int32_t max = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 12);

        if (D_CHECK(DC_COUNT_MIN, count >= 5) && D_CHECK(DC_COUNT_MAX, count <= 50000) &&
            D_CHECK(DC_CAPACITY_MIN, max >= count) && D_CHECK(DC_CAPACITY_MAX, max <= 100000) &&
            D_CHECK(DC_ARRAY_POINTER, is_heap_ptr(actor_data))) {
            D_COUNT(lists_valid); diag_reached(3);
            int check = count < 20 ? count : 20;
            uint64_t ptrs[20];
            diag_stage(DS_ARRAY_CONTENTS);
            if (D_CHECK(DC_ARRAY_READ, read_mem(actor_data, ptrs, check * 8) == check * 8)) {
                D_COUNT(lists_read); diag_reached(4);
                int valid = 0;
                for (int k = 0; k < check; k++)
                    if (is_heap_ptr(ptrs[k])) valid++;
                int score = valid * 100 / check;
                if (has_backref) score += 10;
                diag_path_score(score, valid, check);
                if (D_CHECK(DC_SCORE_IMPROVES, score > best_score) && D_CHECK(DC_THREE_POINTERS, valid >= 3)) {
                    D_COUNT(lists_selected); diag_reached(5);
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

    /* Path 2: scan ULevel for TArray patterns (offset moves between matches) */
    {
        uint8_t lbuf[ULEVEL_SCAN_END];
        ssize_t lread = read_mem(level, lbuf, sizeof(lbuf));
        if (lread >= (ssize_t)(ULEVEL_SCAN_START + 16)) {
            int scan_path = 0;
            for (int off = 0x98; off == 0x98 && off + 16 <= lread; off += ULEVEL_SCAN_STEP) {
                if (scan_path < 4) diag_path(scan_path + 1);
                diag_stage(DS_ARRAY_FIELDS);
                uint64_t actor_data;
                int32_t count, max;
                memcpy(&actor_data, lbuf + off, 8);
                memcpy(&count, lbuf + off + 8, 4);
                memcpy(&max, lbuf + off + 12, 4);

                if (!D_CHECK(DC_COUNT_MIN, count >= 5) || !D_CHECK(DC_COUNT_MAX, count <= 50000) ||
                    !D_CHECK(DC_CAPACITY_MIN, max >= count) || !D_CHECK(DC_CAPACITY_MAX, max <= 100000)) continue;
                if (!D_CHECK(DC_ARRAY_POINTER, is_heap_ptr(actor_data))) continue;
                D_COUNT(lists_valid); diag_reached(3);

                int check = count < 20 ? count : 20;
                uint64_t ptrs[20];
                diag_stage(DS_ARRAY_CONTENTS);
                if (!D_CHECK(DC_ARRAY_READ, read_mem(actor_data, ptrs, check * 8) == check * 8)) continue;
                D_COUNT(lists_read); diag_reached(4);

                int valid = 0;
                for (int k = 0; k < check; k++)
                    if (is_heap_ptr(ptrs[k])) valid++;
                int score = valid * 100 / check;
                if (has_backref) score += 10;
                if (scan_path < 4) diag_path_score(score, valid, check);
                if (D_CHECK(DC_SCORE_IMPROVES, score > best_score) && D_CHECK(DC_THREE_POINTERS, valid >= 3)) {
                    D_COUNT(lists_selected); diag_reached(5);
                    best_score = score;
                    out->level = level;
                    out->actor_data = actor_data;
                    out->actor_count = count;
                    out->level_offset = OFF_UWORLD_PERSISTENT_LEVEL;
                    out->actor_source = off;
                    out->score = score;
                }
                scan_path++;
            }
        }
    }

    diag_path(-1);
    if (D_CHECK(DC_BEST_LIST, best_score > 0)) {
        D_COUNT(worlds_with_list); diag_reached(6); diag_stage(DS_LOCAL_CHAIN);
        uint64_t game_instance = read_ptr(uworld_ptr + 0x190);
        uint64_t local_players = is_any_ptr(game_instance) ? read_ptr(game_instance + 0x38) : 0;
        uint64_t local_player = is_any_ptr(local_players) ? read_ptr(local_players) : 0;
        uint64_t player_controller = is_any_ptr(local_player) ? read_ptr(local_player + 0x30) : 0;
        uint64_t pawn = is_any_ptr(player_controller) ?
            read_ptr(player_controller + OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN) : 0;
        uint64_t local_class = is_any_ptr(pawn) ? read_ptr(pawn + OFF_UOBJECT_CLASS) : 0;

        if (discovery_diag.active) {
            uint64_t chain[] = {game_instance, local_players, local_player, player_controller, pawn, local_class};
            for (int i=0;i<6;i++) if (is_any_ptr(chain[i])) discovery_diag.local_pointer_valid[i]++;
        }
        diag_reached(7);
        /* Actor-pointer validity alone also scores preview/menu worlds highly.
         * Sample the selected list for the concrete pawn layout used by the
         * entity reader so an active gameplay world wins deterministically. */
        int sample_count = out->actor_count < 256 ? out->actor_count : 256;
        uint64_t sample_ptrs[256];
        int plausible_players = 0;
        diag_stage(DS_PLAYER_SAMPLE);
        if (D_CHECK(DC_SAMPLE_NONEMPTY, sample_count > 0) &&
            D_CHECK(DC_SAMPLE_READ, read_mem(out->actor_data, sample_ptrs, (size_t)sample_count * 8) ==
                (ssize_t)((size_t)sample_count * 8))) {
            diag_reached(8);
            for (int i = 0; i < sample_count; i++) {
                D_COUNT(sample_actors);
                uint64_t actor = sample_ptrs[i];
                if (!D_CHECK(DC_SAMPLE_ACTOR_POINTER, is_any_ptr(actor))) continue;
                diag_stage(DS_CLASS_ROOT);
                uint64_t cls = read_ptr(actor + OFF_UOBJECT_CLASS);
                uint64_t root = read_ptr(actor + OFF_AACTOR_ROOT_COMPONENT);
                if (!D_CHECK(DC_ROOT_POINTER, is_any_ptr(root))) continue;
                if (!D_CHECK(DC_LOCAL_CLASS, is_any_ptr(local_class))) continue;
                if (actor != pawn && !D_CHECK(DC_CHARACTER_CLASS, is_character_class(cls, local_class))) continue;
                diag_stage(DS_POSITION);
                float world_position[3];
                if (!read_component_position(root, OFF_USCENE_WORLD_LOCATION, world_position)) continue;
                float x = world_position[0], y = world_position[1], z = world_position[2];
                if (!D_CHECK(DC_X_FINITE, x == x) || !D_CHECK(DC_Y_FINITE, y == y) || !D_CHECK(DC_Z_FINITE, z == z) ||
                    !D_CHECK(DC_X_BOUND, fabsf(x) <= 10000000) || !D_CHECK(DC_Y_BOUND, fabsf(y) <= 10000000) ||
                    !D_CHECK(DC_Z_BOUND, fabsf(z) <= 10000000) ||
                    !D_CHECK(DC_NONZERO_POSITION, !(x == 0 && y == 0 && z == 0))) continue;
                D_COUNT(plausible_players); diag_reached(9);
                plausible_players++;
            }
        }
        out->plausible_players = plausible_players;
        if (D_CHECK(DC_HAS_PLAUSIBLE, plausible_players > 0)) {
            int player_bonus = plausible_players * 20;
            best_score += player_bonus < 60 ? player_bonus : 60;
        }

        /* Prefer the active gameplay world over streaming/preview UWorlds. */
        if (D_CHECK(DC_PC_GAME_HEAP, is_game_heap_ptr(player_controller))) best_score += 30;
        if (D_CHECK(DC_PAWN_GAME_HEAP, is_game_heap_ptr(pawn))) best_score += 30;
        out->score = best_score;
    }
    return best_score;
}

static void scan_for_gworld(void) {
    fprintf(stderr, "[*] Scanning for GWorld (SDK dump offsets: PersistentLevel=0x%x)...\n",
            OFF_UWORLD_PERSISTENT_LEVEL);
    fprintf(stderr, "[*] UE4 base=0x%lx end=0x%lx (%lu MB)\n",
            (unsigned long)ue4_base, (unsigned long)ue4_end,
            (unsigned long)((ue4_end - ue4_base) / (1024*1024)));

    num_candidates = 0;
    diag_begin();

    /* The live SDK dump supplies the actual static GWorld slot.  Checking it
     * is constant-time and avoids walking hundreds of megabytes every 15
     * seconds while the user is in menus.  Retain a rare bounded fallback for
     * a future game build whose offset has changed. */
    static int direct_misses = 0;
    uint64_t direct_addr = ue4_base + OFF_GWORLD_FROM_UE4_BASE;
    uint64_t direct_world = read_ptr(direct_addr);
    if (is_any_ptr(direct_world)) {
        GWorldCandidate direct = {0};
        int score = try_validate_uworld(direct_world, &direct);
        if (score >= 70) {
            direct.gworld_addr = direct_addr;
            direct.uworld = direct_world;
            candidates[num_candidates++] = direct;
            direct_misses = 0;
            fprintf(stderr,
                    "[+] Static GWorld@0x%lx -> UWorld=0x%lx actors=%d players=%d score=%d%%\n",
                    (unsigned long)direct_addr, (unsigned long)direct_world,
                    direct.actor_count, direct.plausible_players, direct.score);
            return;
        }

        /* A valid slot commonly points at the login/loading/front-end world.
         * That is not evidence that the SDK offset changed.  Waiting here is
         * effectively free and prevents a multi-hundred-megabyte compatibility
         * scan from starving the game while it loads resources. */
        direct_misses = 0;
        fprintf(stderr,
                "[*] Static GWorld@0x%lx is a non-gameplay world; waiting for match\n",
                (unsigned long)direct_addr);
        return;
    }

    if (!compatibility_scan || ++direct_misses < 8) {
        fprintf(stderr, "[*] Static GWorld slot is invalid; deferring fallback scan (%d/8)\n",
                direct_misses);
        return;
    }
    direct_misses = 0;
    fprintf(stderr, "[!] Static GWorld did not validate; running compatibility fallback scan\n");

    for (int i = 0; i < num_regions && num_candidates < 64; i++) {
        if (regions[i].perms[0] != 'r' || regions[i].perms[1] != 'w') { diag_mapping(i, DM_NOT_RW); continue; }
        if (regions[i].perms[2] == 'x') { diag_mapping(i, DM_EXECUTABLE); continue; }
        /* Scan libUE4.so rw sections and anonymous regions near UE4 */
        if (!strstr(regions[i].name, "libUE4.so") && regions[i].name[0] != '\0') { diag_mapping(i, DM_NAMED_OTHER); continue; }
        if (regions[i].name[0] == '\0' &&
            (regions[i].start < ue4_base || regions[i].start > ue4_end + 0x10000000)) { diag_mapping(i, DM_ANON_OUTSIDE); continue; }

        uint64_t size = regions[i].end - regions[i].start;
        if (size < 4096 || size > 200 * 1024 * 1024) { diag_mapping(i, DM_SIZE); continue; }
        diag_mapping(i, DM_SELECTED);

        fprintf(stderr, "[*] Scanning rw 0x%lx (%lu MB) %s\n",
                (unsigned long)regions[i].start, (unsigned long)(size / (1024*1024)),
                regions[i].name[0] ? regions[i].name : "(anon)");

        uint64_t addr = regions[i].start;
        while (addr < regions[i].end && num_candidates < 64) {
            size_t chunk = regions[i].end - addr;
            if (chunk > READ_BUF_SIZE) chunk = READ_BUF_SIZE;

            diag_stage(DS_BULK);
            ssize_t n = read_mem(addr, read_buf, chunk);
            if (n > 0 && discovery_diag.mapping_detail >= 0)
                discovery_diag.mappings[discovery_diag.mapping_detail].read_bytes += (uint64_t)n;
            if (n <= 0) { addr += chunk; continue; }

            uint64_t *ptrs = (uint64_t *)read_buf;
            size_t num_ptrs = n / 8;
            for (size_t j = 0; j < num_ptrs && num_candidates < 64; j++) {
                D_COUNT(slots);
                if (discovery_diag.mapping_detail >= 0) discovery_diag.mappings[discovery_diag.mapping_detail].slots++;
                uint64_t val = ptrs[j];
                if (!is_heap_ptr(val)) continue;
                D_COUNT(potential);
                diag_candidate_begin(val);

                GWorldCandidate cand = {0};
                int score = try_validate_uworld(val, &cand);
                int qualifies = D_CHECK(DC_SCORE_THRESHOLD, score >= 40);
                diag_candidate_finish(score, cand.plausible_players);
                if (qualifies) {
                    cand.gworld_addr = addr + j * 8;
                    cand.uworld = val;
                    candidates[num_candidates++] = cand;

                    char src_desc[64];
                    if (cand.actor_source == 0) snprintf(src_desc, sizeof(src_desc), "ActorCluster(0xC8->0x28)");
                    else snprintf(src_desc, sizeof(src_desc), "DirectLevel+0x%x", cand.actor_source);
                    const char *src = src_desc;
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
    diag_report(stderr);
}

/* GWorld is referenced by several UE globals and subsystem mirrors in this
 * build. A discovered mirror can be cleared while the verified UWorld remains
 * active. Find another nearby reference to that exact world rather than
 * rescanning every writable mapping (which stalls the overlay for seconds). */
static int recover_nearby_world_alias(uint64_t old_addr, uint64_t expected_world,
                                      uint64_t *new_addr, int *new_actor_source) {
    if (!is_any_ptr(expected_world) || !is_any_ptr(read_ptr(
            expected_world + OFF_UWORLD_PERSISTENT_LEVEL))) return 0;

    for (int ri = 0; ri < num_regions; ri++) {
        if (old_addr < regions[ri].start || old_addr >= regions[ri].end) continue;
        uint64_t radius = 0x200000; /* bounded 4 MiB window */
        uint64_t start = old_addr > radius ? old_addr - radius : regions[ri].start;
        if (start < regions[ri].start) start = regions[ri].start;
        start &= ~7ULL;
        uint64_t end = old_addr + radius;
        if (end > regions[ri].end) end = regions[ri].end;

        for (uint64_t addr = start; addr < end;) {
            size_t chunk = (size_t)(end - addr);
            if (chunk > READ_BUF_SIZE) chunk = READ_BUF_SIZE;
            ssize_t nread = read_mem(addr, read_buf, chunk);
            if (nread <= 0) { addr += chunk; continue; }
            uint64_t *values = (uint64_t *)read_buf;
            size_t count = (size_t)nread / sizeof(uint64_t);
            for (size_t i = 0; i < count; i++) {
                if (values[i] != expected_world) continue;
                uint64_t candidate_addr = addr + i * sizeof(uint64_t);
                /* The failed mirror may become readable again for one sample,
                 * but selecting it just repeats the same outage. Fail over to
                 * a genuinely different reference. */
                if (candidate_addr == old_addr) continue;
                GWorldCandidate candidate = {0};
                if (try_validate_uworld(expected_world, &candidate) < 70 ||
                    candidate.plausible_players <= 0) continue;
                *new_addr = candidate_addr;
                *new_actor_source = candidate.actor_source;
                return 1;
            }
            addr += chunk;
        }
        break;
    }
    return 0;
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
        actor_data = read_ptr(level + cand->actor_source);
        actor_count = read_i32(level + cand->actor_source + 8);
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

        /* Scan root component offsets in a wide range covering both old (0x180) and new (0x108) */
        for (int roff = 0x100; roff <= 0x1A0; roff += 8) {
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
/* Delta Force name encoding, verified against the matching SDK dump.
 * Reference: MobileDumper-7 DeltaForceProfile at 595655004a8e1d56fd0e708fe64649f1864d7066.
 * Only the string bytes are encoded; the two-byte entry header is ordinary UE4. */
static void decode_name_bytes(char *data, int len) {
    unsigned n = (unsigned)len, key;
    switch (n % 9) {
        case 0: key = (n & 0x1f) + n; break;
        case 1: key = (n ^ 0xdf) + n; break;
        case 2: key = (n | 0xcf) + n; break;
        case 3: key = 33 * n; break;
        case 4: key = n + (n >> 2); break;
        case 5: key = 3 * n + 5; break;
        case 6: key = ((4 * n) | 5) + n; break;
        case 7: key = ((n >> 4) | 7) + n; break;
        default: key = (n ^ 0xc) + n; break;
    }
    for (int i = 0; i < len; i++) data[i] = (char)((key & 0x80) ^ (uint8_t)~data[i]);
}

static int resolve_fname(int32_t idx, char *out, int out_size) {
    if (fname_pool_addr == 0 || idx < 0) { out[0] = 0; return 0; }

    int block_idx = idx >> 18;
    int entry_off = (idx & ((1 << 18) - 1)) * 2;

    if (block_idx < 0 || block_idx >= 8192) { out[0] = 0; return 0; }
    uint64_t block_ptr = fname_block_cache[block_idx];
    if (!block_ptr) {
        block_ptr = read_ptr(fname_pool_addr + fname_blocks_off + block_idx * 8);
        if (is_any_ptr(block_ptr)) fname_block_cache[block_idx] = block_ptr;
    }
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

    if (hdr_buf[0] & 1) { out[0] = 0; return 0; } /* Class identifiers are ANSI. */
    if (read_mem(block_ptr + entry_off + hdr_sz, out, len) != len) { out[0] = 0; return 0; }
    decode_name_bytes(out, len);
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)out[i];
        if (c < 0x20 || c > 0x7e) { out[0] = 0; return 0; }
    }
    out[len] = 0;
    return len;
}

/* Reflection names may be unreadable. The matching SDK dump also supplies
 * native registration indices. Cross-check their immediate parent before
 * using these exact-build anchors; never label dynamic Blueprint indices. */
static int resolve_class_name(uint64_t cls, char *out, int size) {
    if (fname_pool_addr && resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), out, size)) return 1;
    static const struct { int index, parent; const char *name; } anchors[] = {
#include "sdk_native_classes.inc"
    };
    int index = read_i32(cls + 0x24);
    for (unsigned i = 0; i < sizeof(anchors)/sizeof(anchors[0]); i++) {
        if (anchors[i].index != index) continue;
        uint64_t parent = read_ptr(cls + 0x40);
        if (!is_any_ptr(parent) || read_i32(parent + 0x24) != anchors[i].parent) break;
        snprintf(out, (size_t)size, "%s", anchors[i].name);
        return 1;
    }
    out[0] = 0;
    return 0;
}

/* ========== RUNNING MODE: Read actors and POST to radar ========== */

static int find_object_field(uint64_t owner, int start, int end, const char *class_fragment);

#define NUM_SKELETON_BONES 16
#include "loot_value.h"
static LootValueReader loot_values;
static int loot_values_fd = -1, loot_values_fd_pid = -1;
static int read_loot_value_memory(void *unused, uint64_t address, void *out, size_t size) {
    (void)unused;
    if (read_mem(address, out, size) == (ssize_t)size) return 1;
    /* Only the known Lua main-state pointer may need a force read. Never
     * switch the scanner's general reader or class-name behavior. */
    if (address != ue4_base + 0x1fc9cd70 || size != 8) return 0;
    if (loot_values_fd_pid != game_pid) {
        if (loot_values_fd >= 0) close(loot_values_fd);
        char path[80]; snprintf(path, sizeof(path), "/proc/%d/mem", game_pid);
        loot_values_fd = open(path, O_RDONLY | O_CLOEXEC); loot_values_fd_pid = game_pid;
    }
    return loot_values_fd >= 0 && pread(loot_values_fd, out, size, (off_t)address) == (ssize_t)size;
}
/* Bone indices: 0=head 1=neck 2=spine_upper 3=pelvis
 * 4=l_shoulder 5=l_elbow 6=l_hand 7=r_shoulder 8=r_elbow 9=r_hand
 * 10=l_hip 11=l_knee 12=l_foot 13=r_hip 14=r_knee 15=r_foot */

typedef struct {
    char id[32];
    float x, y, z, yaw, pitch, fov;
    char name[64];
    char kind[16];
    float health, max_health, half_height;
    int health_known;
    int dead;
    int team;
    int native_team;
    uint64_t player_state;
    float bone_pos[NUM_SKELETON_BONES][3]; /* world-space bone positions in meters */
    int has_bones;
    LootValue loot;
} Entity;

static Entity entities[MAX_ACTORS];
static int num_entities = 0;
/* Verified live on the current mobile build. Dynamic resolution below is only
 * a version-drift fallback, not part of the normal frame/launch path. */
static int resolved_team_off = OFF_GP_PLAYER_STATE_TEAM_ID;
static unsigned team_resolve_retry = 0;

/* PlayerState moves between regional/mobile builds. Resolve the squad/team
 * field from the live roster rather than silently treating a stale zero-valued
 * SDK offset as "everyone is friendly". A real squad field groups the local
 * player with a small party and does not put most of the lobby in one bucket. */
static int resolve_team_field(const char *self_id) {
    int human[MAX_ACTORS];
    int human_count = 0;
    int self_human = -1;
    for (int i = 0; i < num_entities; i++) {
        if (strcmp(entities[i].kind, "player") != 0 ||
            !is_game_heap_ptr(entities[i].player_state)) continue;
        human[human_count] = i;
        if (strcmp(entities[i].id, self_id) == 0) self_human = human_count;
        human_count++;
    }
    if (self_human < 0 || human_count < 3) return -1;

    int best_off = -1;
    int best_score = -100000;
    int best_self_count = 0;
    int best_distinct = 0;
    int best_value = -1;
    for (int off = 0x300; off <= 0xA00; off += 4) {
        int values[MAX_ACTORS];
        int valid = 1;
        for (int h = 0; h < human_count; h++) {
            values[h] = read_i32(entities[human[h]].player_state + (uint64_t)off);
            if (values[h] < 0 || values[h] > 1000000000) { valid = 0; break; }
        }
        if (!valid) continue;

        int distinct = 0;
        int max_group = 0;
        int self_count = 0;
        for (int h = 0; h < human_count; h++) {
            int first = 1;
            int group = 0;
            for (int k = 0; k < human_count; k++) {
                if (values[k] == values[h]) group++;
                if (k < h && values[k] == values[h]) first = 0;
            }
            if (first) distinct++;
            if (group > max_group) max_group = group;
            if (values[h] == values[self_human]) self_count++;
        }
        if (distinct < 2 || max_group > 4 || self_count > 4) continue;

        int score = self_count == 3 ? 240 :
                    self_count == 2 ? 190 :
                    self_count == 4 ? 145 : 25;
        score += max_group == self_count ? 12 : 0;
        int sdk_delta = abs(off - OFF_GP_PLAYER_STATE_TEAM_ID);
        score -= sdk_delta / 32;
        if (score > best_score) {
            best_score = score;
            best_off = off;
            best_self_count = self_count;
            best_distinct = distinct;
            best_value = values[self_human];
        }
    }

    /* A unique per-player identifier is not a team field. */
    if (best_off >= 0 && best_self_count >= 2) {
        fprintf(stderr,
                "\n[+] Resolved PlayerState team field +0x%x self=%d same=%d distinct=%d score=%d\n",
                best_off, best_value, best_self_count, best_distinct, best_score);
        return best_off;
    }
    return -1;
}

/* ========== SKELETON / BONE READING ========== */
static int resolved_mesh_off = -2;  /* -2 = not searched, -1 = not found */
static int resolved_bone_array_off = -2;
static int bone_map[NUM_SKELETON_BONES]; /* maps our bone index to game bone index */
static int bone_map_ready = 0;
static int bone_stride = 48; /* FTransform size: 48 bytes for float-based (mobile) */

/* Find mesh component on the self pawn using reflection */
static int find_mesh_component(uint64_t pawn) {
    int off = find_object_field(pawn, 0x380, 0xC00, "SkeletalMesh");
    if (off >= 0) {
        fprintf(stderr, "[+] Found SkeletalMesh field at pawn+0x%x\n", off);
        return off;
    }
    /* ACharacter::Mesh is a named property; try broader class fragment */
    off = find_object_field(pawn, 0x380, 0xC00, "SkinnedMesh");
    if (off >= 0) {
        fprintf(stderr, "[+] Found SkinnedMesh field at pawn+0x%x\n", off);
        return off;
    }
    return -1;
}

/* Search bone names in a SkeletalMesh's RefSkeleton to build bone_map.
 * RefSkeleton contains a TArray<FMeshBoneInfo> where each entry has
 * an FName (4-byte index) at the start. We scan the bone name FNames
 * and match against our target bone names. */
static void discover_bone_map(uint64_t mesh_comp) {
    bone_map_ready = 0;
    for (int i = 0; i < NUM_SKELETON_BONES; i++) bone_map[i] = -1;

    /* Find bone array: scan mesh component for TArray with 20-200 entries
     * whose element stride looks like FTransform (check scale ~1.0) */
    int bone_count = -1;
    uint64_t bone_data = 0;
    for (int off = 0x200; off < 0xE00; off += 8) {
        uint64_t ptr = read_ptr(mesh_comp + off);
        if (!is_any_ptr(ptr)) continue;
        int32_t count = read_i32(mesh_comp + off + 8);
        if (count < 20 || count > 300) continue;

        /* Validate: check if elements look like FTransforms.
         * FTransform with floats: quat(16) + trans(16) + scale(16) = 48 bytes
         * Check scale of bone 0 (should be near 1.0) */
        float sx = read_float(ptr + 32); /* scale X at offset 32 within FTransform */
        float sy = read_float(ptr + 36);
        float sz = read_float(ptr + 40);
        if (fabsf(sx - 1.0f) < 0.5f && fabsf(sy - 1.0f) < 0.5f && fabsf(sz - 1.0f) < 0.5f) {
            bone_data = ptr;
            bone_count = count;
            resolved_bone_array_off = off;
            bone_stride = 48;
            break;
        }
        /* Try 80-byte stride (doubles): quat(32) + trans(32) + scale(16) */
        double dsx, dsy;
        read_mem(ptr + 64, &dsx, 8);
        read_mem(ptr + 72, &dsy, 8);
        if (fabs(dsx - 1.0) < 0.5 && fabs(dsy - 1.0) < 0.5) {
            bone_data = ptr;
            bone_count = count;
            resolved_bone_array_off = off;
            bone_stride = 80;
            break;
        }
    }
    if (bone_count < 0) {
        fprintf(stderr, "[!] No bone array found in mesh component\n");
        return;
    }
    fprintf(stderr, "[+] Bone array: comp+0x%x count=%d stride=%d\n",
            resolved_bone_array_off, bone_count, bone_stride);

    /* Try to read bone names from the SkeletalMesh asset.
     * USkeletalMeshComponent has a SkeletalMesh pointer (usually 0x5A0-0x600).
     * The SkeletalMesh has a RefSkeleton with FName bone names.
     * If we can't find names, use positional heuristics. */

    /* Positional heuristic: read all bone translations, identify bones by
     * their positions relative to the root (bone 0 = pelvis/root).
     * This works because humanoid skeletons have predictable layouts:
     * - Head is highest
     * - Hands are widest laterally
     * - Feet are lowest
     * We use component-space transforms where Y is typically up. */
    float bpos[300][3];
    int valid = 0;
    for (int b = 0; b < bone_count && b < 300; b++) {
        uint64_t base = bone_data + (uint64_t)b * bone_stride;
        if (bone_stride == 48) {
            bpos[b][0] = read_float(base + 16); /* trans X */
            bpos[b][1] = read_float(base + 20); /* trans Y */
            bpos[b][2] = read_float(base + 24); /* trans Z */
        } else {
            double dx, dy, dz;
            read_mem(base + 32, &dx, 8);
            read_mem(base + 40, &dy, 8);
            read_mem(base + 48, &dz, 8);
            bpos[b][0] = (float)dx;
            bpos[b][1] = (float)dy;
            bpos[b][2] = (float)dz;
        }
        if (bpos[b][0] == bpos[b][0]) valid++;
    }
    fprintf(stderr, "[+] Read %d/%d valid bone positions\n", valid, bone_count);

    /* Log first 20 bone positions for diagnostics */
    for (int b = 0; b < bone_count && b < 20; b++) {
        fprintf(stderr, "  bone[%d] pos=(%.1f, %.1f, %.1f) cm\n",
                b, bpos[b][0], bpos[b][1], bpos[b][2]);
    }

    /* Find head: highest bone (max component-space Z or X depending on orientation) */
    /* For now, use bone indices that are common in UE4 humanoid rigs:
     * 0=root/pelvis, head is usually around index 5-10, limbs are higher */
    /* We'll use a simple heuristic: find the bone with highest altitude */
    float max_up = -99999;
    int head_idx = -1;
    for (int b = 1; b < bone_count; b++) {
        /* Try X as up (UE4 component space varies) */
        float up = bpos[b][0]; /* we'll try all axes */
        if (fabsf(bpos[b][0]) + fabsf(bpos[b][1]) + fabsf(bpos[b][2]) < 1.0f) continue;
        /* Use whichever axis has the largest range as "up" */
        if (up > max_up) { max_up = up; head_idx = b; }
    }
    if (head_idx >= 0) {
        fprintf(stderr, "[+] Candidate head bone: %d at (%.1f, %.1f, %.1f)\n",
                head_idx, bpos[head_idx][0], bpos[head_idx][1], bpos[head_idx][2]);
    }

    /* For now, set a default mapping using common UE4 mannequin-style indices.
     * This will be refined once we see actual bone positions. */
    /* Default humanoid skeleton (UE4 mannequin-like):
     * 0=root, 1=pelvis, 2=spine_01, 3=spine_02, 4=spine_03,
     * 5=neck_01, 6=head,
     * Left arm chain starts around 7-11, right arm around 30+
     * Left leg around 55+, right leg around 60+ */
    /* These are placeholders — the diagnostic output will tell us the real indices */
    bone_map[0] = 6;   /* head */
    bone_map[1] = 5;   /* neck */
    bone_map[2] = 3;   /* spine_02 */
    bone_map[3] = 1;   /* pelvis */
    bone_map[4] = 8;   /* l_shoulder */
    bone_map[5] = 9;   /* l_elbow */
    bone_map[6] = 10;  /* l_hand */
    bone_map[7] = 31;  /* r_shoulder */
    bone_map[8] = 32;  /* r_elbow */
    bone_map[9] = 33;  /* r_hand */
    bone_map[10] = 55; /* l_hip */
    bone_map[11] = 56; /* l_knee */
    bone_map[12] = 57; /* l_foot */
    bone_map[13] = 60; /* r_hip */
    bone_map[14] = 61; /* r_knee */
    bone_map[15] = 62; /* r_foot */

    /* Clamp to valid range */
    for (int i = 0; i < NUM_SKELETON_BONES; i++)
        if (bone_map[i] >= bone_count) bone_map[i] = -1;

    bone_map_ready = 1;
}

/* Read bone world positions for an entity */
static void read_entity_bones(Entity *e, uint64_t actor, uint64_t root, int mesh_off, int pos_off) {
    e->has_bones = 0;
    if (!bone_map_ready || mesh_off < 0 || resolved_bone_array_off < 0) return;

    uint64_t mesh_comp = read_ptr(actor + mesh_off);
    if (!is_any_ptr(mesh_comp)) return;

    uint64_t bone_data = read_ptr(mesh_comp + resolved_bone_array_off);
    int32_t bone_count = read_i32(mesh_comp + resolved_bone_array_off + 8);
    if (!is_any_ptr(bone_data) || bone_count < 20) return;

    /* Read mesh component's world transform for converting component-space to world-space.
     * The mesh component's ComponentToWorld is at the same offset as the root component's. */
    float mesh_world_x = read_float(root + pos_off) / 100.0f;
    float mesh_world_y = read_float(root + pos_off + 4) / 100.0f;
    float mesh_world_z = read_float(root + pos_off + 8) / 100.0f;

    for (int i = 0; i < NUM_SKELETON_BONES; i++) {
        int bidx = bone_map[i];
        if (bidx < 0 || bidx >= bone_count) {
            e->bone_pos[i][0] = e->x;
            e->bone_pos[i][1] = e->y;
            e->bone_pos[i][2] = e->z;
            continue;
        }
        uint64_t base = bone_data + (uint64_t)bidx * bone_stride;
        float bx, by, bz;
        if (bone_stride == 48) {
            bx = read_float(base + 16) / 100.0f;
            by = read_float(base + 20) / 100.0f;
            bz = read_float(base + 24) / 100.0f;
        } else {
            double dx, dy, dz;
            read_mem(base + 32, &dx, 8);
            read_mem(base + 40, &dy, 8);
            read_mem(base + 48, &dz, 8);
            bx = (float)(dx / 100.0);
            by = (float)(dy / 100.0);
            bz = (float)(dz / 100.0);
        }
        /* Component-space bone positions — add entity world position */
        e->bone_pos[i][0] = mesh_world_x + bx;
        e->bone_pos[i][1] = mesh_world_y + by;
        e->bone_pos[i][2] = mesh_world_z + bz;
    }
    e->has_bones = 1;
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
#define OFF_GP_CAMERA_FPP_FOV 0x4FD0
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
static unsigned stale_camera_frames = 0;
static int resolved_camera_manager_off = -1;
static int resolved_camera_cache_off = -1;
static int camera_candidate_offsets[128];
static float camera_candidate_timestamps[128];
static int camera_candidate_count = 0;

/* Remote network pawns normally have no Controller on a client. Identify
 * combatants through their reflected class hierarchy instead: the local pawn's
 * direct parent is the common character base used by human and AI pawns. */
static int is_character_class(uint64_t cls, uint64_t local_class) {
    if (!is_any_ptr(cls) || !is_any_ptr(local_class)) return 0;
    {
        uint64_t current = cls;
        for (int depth = 0; depth < 16 && is_any_ptr(current); depth++) {
            char name[96] = {0};
            resolve_class_name(current, name, sizeof(name));
            if (!strcmp(name, "GPCharacterBase") || !strcmp(name, "GPCharacter")) return 1;
            uint64_t parent = read_ptr(current + 0x40);
            if (parent == current) break;
            current = parent;
        }
    }

    return 0;
}

/* Cache the reflected inheritance result per UClass. Loot actors are not
 * character-shaped, so they must be admitted explicitly from the level actor
 * arrays. Matching the parent chain also covers map-specific Blueprint classes
 * such as bird nests and stashes. */
typedef struct { uint64_t cls; int kind; } LootClassCache;
static LootClassCache loot_class_cache[256];
static int loot_class_cache_count = 0;

/* Return the display kind from the reflected class hierarchy:
 * 0=not loot, 1=item, 2=container, 3=stash/interactable, 4=death box. */
static int loot_kind_from_class(uint64_t cls) {
    if (!is_any_ptr(cls)) return 0;
    for (int i = 0; i < loot_class_cache_count; i++)
        if (loot_class_cache[i].cls == cls) return loot_class_cache[i].kind;

    uint64_t original = cls;
    int result = 0;
    for (int depth = 0; depth < 16 && is_any_ptr(cls); depth++) {
        char name[96] = {0};
        resolve_class_name(cls, name, sizeof(name));
        if (strstr(name, "InventoryPickup_DeadBody")) result = 4;
        else if (!result && (strstr(name, "SingleItemContainer") ||
                 strstr(name, "ChangeableContainer"))) result = 3;
        else if (!result && (strstr(name, "InventoryPickup_Container") ||
                 strstr(name, "InventoryPickup_OpenBox"))) result = 2;
        else if (!result && (strstr(name, "InventoryPickup") ||
                 strstr(name, "PickupBase") || strstr(name, "PlunderPickup"))) result = 1;
        uint64_t next = read_ptr(cls + 0x40); /* UStruct::SuperStruct */
        if (next == cls) break;
        cls = next;
    }
    if (loot_class_cache_count < (int)(sizeof(loot_class_cache) / sizeof(loot_class_cache[0]))) {
        loot_class_cache[loot_class_cache_count].cls = original;
        loot_class_cache[loot_class_cache_count].kind = result;
        loot_class_cache_count++;
    }
    return result;
}

static int loot_metadata_kind(uint64_t actor, int kind) {
    if (kind == 3) return 3;
    uint64_t cls = read_ptr(actor + OFF_UOBJECT_CLASS);
    static struct { uint64_t cls; int index, kind, result; } cache[128];
    static unsigned count;
    uint64_t original = cls; int index = read_i32(cls + 0x24);
    for (unsigned i = 0; i < count; i++)
        if (cache[i].cls == cls && cache[i].index == index && cache[i].kind == kind) return cache[i].result;
    int result = 0;
    for (int depth = 0; depth < 16 && is_any_ptr(cls); depth++) {
        uint64_t parent = read_ptr(cls + 0x40);
        if (read_i32(cls + 0x24) == 7824 && read_i32(parent + 0x24) == 7823) { result = kind; break; }
        if (parent == cls) break;
        cls = parent;
    }
    if (count < 128) {
        cache[count].cls = original; cache[count].index = index;
        cache[count].kind = kind; cache[count++].result = result;
    }
    return result;
}

static int sane_tarray(uint64_t owner, int off, int max_count) {
    uint64_t data = read_ptr(owner + (uint64_t)off);
    int count = read_i32(owner + (uint64_t)off + 8);
    int capacity = read_i32(owner + (uint64_t)off + 12);
    if (count < 0 || count > max_count || capacity < count || capacity > max_count * 4) return 0;
    return count == 0 || is_game_heap_ptr(data);
}

/* UGPBattleFieldSystem is a UWorldSubsystem. Its shipped layout provides the
 * authoritative client character arrays, plus exact local controller/pawn
 * back-references at +0x88/+0x90. Resolve it from the world's embedded
 * subsystem collection without relying on the broken regional FNamePool. */
static int is_battlefield_system(uint64_t object, uint64_t local_pc,
                                 uint64_t local_pawn) {
    if (!is_game_heap_ptr(object) || read_ptr(object + 0x88) != local_pc ||
        read_ptr(object + 0x90) != local_pawn || !sane_tarray(object, 0x110, 512))
        return 0;
    uint64_t data = read_ptr(object + 0x110);
    int count = read_i32(object + 0x118);
    if (!is_game_heap_ptr(data) || count <= 0) return 0;
    uint64_t members[512];
    if (read_mem(data, members, (size_t)count * sizeof(uint64_t)) !=
        (ssize_t)((size_t)count * sizeof(uint64_t))) return 0;
    for (int i = 0; i < count; i++)
        if (members[i] == local_pawn) return 1;
    return 0;
}

static uint64_t find_battlefield_system(uint64_t uworld, uint64_t local_pc,
                                        uint64_t local_pawn) {
    if (!is_any_ptr(uworld) || !is_game_heap_ptr(local_pc) ||
        !is_game_heap_ptr(local_pawn)) return 0;

    uint64_t world_fields[0x810 / 8] = {0};
    if (read_mem(uworld, world_fields, sizeof(world_fields)) !=
        (ssize_t)sizeof(world_fields)) return 0;

    /* First test direct world fields, then allocations referenced by the
     * padded subsystem-collection portion of UWorld. */
    for (int pass = 0; pass < 2; pass++) {
        int first = pass == 0 ? 0 : 0x228 / 8;
        int last = (int)(sizeof(world_fields) / sizeof(world_fields[0]));
        for (int wi = first; wi < last; wi++) {
            uint64_t root = world_fields[wi];
            if (!is_game_heap_ptr(root)) continue;
            if (is_battlefield_system(root, local_pc, local_pawn)) return root;
            if (pass == 0) continue;

            uint64_t referenced[0x1000 / 8] = {0};
            ssize_t got = read_mem(root, referenced, sizeof(referenced));
            if (got <= 0) continue;
            int count = (int)((size_t)got / sizeof(uint64_t));
            for (int ri = 0; ri < count; ri++) {
                uint64_t candidate = referenced[ri];
                if (!is_game_heap_ptr(candidate)) continue;
                if (is_battlefield_system(candidate, local_pc, local_pawn)) return candidate;
            }
        }
    }
    return 0;
}

/* Fallback for builds where the subsystem collection's private map is not
 * represented in the SDK dump. Search for the exact adjacent
 * LocalPlayerController/LocalPlayerPawn pair; this is performed once per world
 * and stops at the first structurally verified roster. */
static uint64_t scan_battlefield_system(uint64_t local_pc, uint64_t local_pawn) {
    for (int pass = 0; pass < 2; pass++) {
        for (int ri = 0; ri < num_regions; ri++) {
            if (regions[ri].perms[0] != 'r' || regions[ri].perms[1] != 'w' ||
                regions[ri].perms[2] == 'x') continue;
            uint64_t size = regions[ri].end - regions[ri].start;
            if (size < 4096 || size > 256ULL * 1024 * 1024) continue;
            int near_player_heap = (local_pc >= regions[ri].start -
                (regions[ri].start > 0x20000000 ? 0x20000000 : regions[ri].start) &&
                local_pc < regions[ri].end + 0x20000000) ||
                (local_pawn >= regions[ri].start -
                (regions[ri].start > 0x20000000 ? 0x20000000 : regions[ri].start) &&
                local_pawn < regions[ri].end + 0x20000000);
            if ((pass == 0) != near_player_heap) continue;

            for (uint64_t addr = regions[ri].start; addr < regions[ri].end;) {
                size_t chunk = (size_t)(regions[ri].end - addr);
                if (chunk > READ_BUF_SIZE) chunk = READ_BUF_SIZE;
                ssize_t got = read_mem(addr, read_buf, chunk);
                if (got <= 0) { addr += chunk; continue; }
                uint64_t *values = (uint64_t *)read_buf;
                size_t count = (size_t)got / sizeof(uint64_t);
                for (size_t i = 0; i + 1 < count; i++) {
                    if (values[i] != local_pc || values[i + 1] != local_pawn) continue;
                    uint64_t pair_addr = addr + i * sizeof(uint64_t);
                    if (pair_addr < 0x88) continue;
                    uint64_t candidate = pair_addr - 0x88;
                    if (is_battlefield_system(candidate, local_pc, local_pawn))
                        return candidate;
                }
                addr += chunk;
            }
        }
    }
    return 0;
}

static int read_authoritative_characters(uint64_t system, uint64_t local_pawn,
                                          uint64_t *out, int capacity) {
    if (!is_game_heap_ptr(system) || capacity <= 0) return 0;
    uint64_t data = read_ptr(system + 0x110);
    int count = read_i32(system + 0x118);
    int max = read_i32(system + 0x11C);
    if (!is_game_heap_ptr(data) || count <= 0 || count > capacity ||
        max < count || max > 2048) return 0;
    if (read_mem(data, out, (size_t)count * sizeof(uint64_t)) !=
        (ssize_t)((size_t)count * sizeof(uint64_t))) return 0;
    int valid = 0, has_local = 0;
    for (int i = 0; i < count; i++) {
        if (!is_game_heap_ptr(out[i])) continue;
        valid++;
        if (out[i] == local_pawn) has_local = 1;
    }
    return has_local && valid >= 1 ? count : 0;
}

/* The small live combat roster can change between expensive level/loot scans.
 * Preserve cached loot, reserving a full cache's slots for new combatants. */
static int update_cached_combatants(uint64_t *actors, uint8_t *kinds,
                                    uint8_t *authoritative, int count, int capacity,
                                    const uint64_t *roster, int roster_count) {
    for (int i = 0; i < roster_count; i++) {
        if (!is_game_heap_ptr(roster[i])) continue;
        int slot = -1;
        for (int j = 0; j < count; j++)
            if (actors[j] == roster[i]) { slot = j; break; }
        if (slot < 0 && count < capacity) slot = count++;
        if (slot < 0) {
            for (int j = count - 1; j >= 0; j--)
                if (kinds[j] != 0) { slot = j; break; }
        }
        if (slot < 0) continue;
        actors[slot] = roster[i]; kinds[slot] = 0; authoritative[slot] = 1;
    }
    return count;
}

/* Regional FNamePool encoding currently prevents dependable Blueprint class
 * strings. These signatures come from the shipped class layouts and require
 * several independent fields, avoiding a broad scan for arbitrary AActors. */
/* 0=not loot, 1=item, 2=container, 3=single-item stash/interactable,
 * 4=death box. */
static int classify_loot_actor(uint64_t actor) {
    /* Dead-body containers are not guaranteed to retain a populated
     * APickupBase inventory descriptor (AI boxes commonly do not). Test the
     * derived-class fields independently of the generic pickup score. */
    char dead_player_name[64] = {0};
    read_fstring(actor + 0x2638, dead_player_name, sizeof(dead_player_name));
    uint64_t dead_mesh = read_ptr(actor + 0x2688);
    uint64_t looting_owner = read_ptr(actor + 0x2780);
    int camp_id = read_i32(actor + 0x2670);
    float dead_currency = read_float(actor + 0x2678);
    uint8_t dead_flags[2] = {0};
    read_mem(actor + 0x277A, &dead_flags[0], 1); /* bIsAI */
    read_mem(actor + 0x2790, &dead_flags[1], 1); /* bLooted */
    int deadbox_score = 0;
    if (dead_player_name[0]) deadbox_score += 3;
    if (is_game_heap_ptr(dead_mesh)) deadbox_score += 3;
    if (is_game_heap_ptr(looting_owner)) deadbox_score += 2;
    if (camp_id >= -1 && camp_id <= 32) deadbox_score++;
    if (isfinite(dead_currency) && dead_currency >= 0.0f &&
        dead_currency <= 1000000000.0f) deadbox_score++;
    if ((dead_flags[0] & 0xfe) == 0 && (dead_flags[1] & 0xfe) == 0)
        deadbox_score++;
    if (deadbox_score >= 7) return 4;

    int pickup_score = 0;
    uint64_t gid = read_ptr(actor + 0xBC8);       /* APickupBase::InventoryGID */
    uint64_t inventory_type = read_ptr(actor + 0xBE8);
    int stack_count = read_i32(actor + 0xBF0);
    float draw_distance = read_float(actor + 0xBDC);
    float unit_scale = read_float(actor + 0xC0C);
    int priority = read_i32(actor + 0xC10);
    uint64_t sphere = read_ptr(actor + 0x1660);   /* AInventoryPickup::Sphere */
    uint8_t pickup_container = 0;
    read_mem(actor + 0x16B8, &pickup_container, 1);
    if (gid > 0 && gid < 0x7fffffffffffffffULL) pickup_score += 3;
    if (is_game_heap_ptr(inventory_type)) pickup_score += 2;
    if (stack_count >= 0 && stack_count <= 100000) pickup_score++;
    if (isfinite(draw_distance) && draw_distance >= 0.0f && draw_distance <= 10000000.0f) pickup_score++;
    if (isfinite(unit_scale) && unit_scale >= 0.01f && unit_scale <= 100.0f) pickup_score++;
    if (priority >= -10 && priority <= 10000) pickup_score++;
    if (is_game_heap_ptr(sphere)) pickup_score += 2;
    if (pickup_container & 1) pickup_score += 2;
    if (pickup_score >= 8) {
        if (dead_player_name[0] &&
            (is_game_heap_ptr(dead_mesh) ||
             (camp_id >= -1 && camp_id <= 32 && isfinite(dead_currency) &&
              dead_currency >= 0.0f && dead_currency <= 1000000000.0f))) return 4;
        if (pickup_container & 1) return 2;
        return 1;
    }

    int container_score = 0;
    int box_id = read_i32(actor + 0xCB8);         /* Interactor_SingleItemContainer */
    uint64_t mesh = read_ptr(actor + 0xCC0);
    uint64_t open_close = read_ptr(actor + 0xCC8);
    uint64_t switcher = read_ptr(actor + 0xCD0);
    uint64_t open_box = read_ptr(actor + 0xD70);
    uint8_t container_type = 0;
    read_mem(actor + 0xD28, &container_type, 1);
    if (box_id > 0) container_score++;
    if (is_game_heap_ptr(mesh)) container_score += 2;
    if (is_game_heap_ptr(open_close)) container_score += 2;
    if (is_game_heap_ptr(switcher)) container_score += 2;
    if (is_game_heap_ptr(open_box)) container_score += 2;
    if (sane_tarray(actor, 0xD08, 128)) container_score++;
    if (sane_tarray(actor, 0xD18, 128)) container_score++;
    if (container_type < 32) container_score++;
    return container_score >= 7 ? 3 : 0;
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
        resolve_class_name(cls, class_name, sizeof(class_name));
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
        if (!is_game_heap_ptr(candidate)) continue;
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
    uint64_t uworld = read_ptr(gworld_addr);
    if (!is_any_ptr(uworld)) return -1;
    if (verified_uworld != uworld) {
        verified_uworld = uworld;
        match_was_verified = 0;
        last_camera_timestamp = -1.0f;
        stale_camera_frames = 0;
        health_layout_count = 0;
        health_layout_clock = 0;
        memset(loot_values.actors, 0, sizeof(loot_values.actors));
        loot_values.next_actor = 0;
        /* The field layout belongs to this game process/build, not to one
         * transient UWorld. Keep a successful resolution across loading-world
         * swaps; the scanner itself restarts when the game PID changes. */
    }

    uint64_t level = read_ptr(uworld + OFF_UWORLD_PERSISTENT_LEVEL);
    if (!is_any_ptr(level)) return -1;

    uint64_t actor_data;
    int32_t actor_count;

    if (actor_source == 0) {
        uint64_t cluster = read_ptr(level + OFF_ULEVEL_ACTOR_CLUSTER);
        if (!is_any_ptr(cluster)) return -1;
        actor_data = read_ptr(cluster + OFF_ACTOR_CONTAINER_ACTORS);
        actor_count = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 8);
    } else {
        actor_data = read_ptr(level + actor_source);
        actor_count = read_i32(level + actor_source + 8);
    }

    if (!is_any_ptr(actor_data) || actor_count <= 0 || actor_count > 50000) return -1;

    self_entity_id[0] = 0;
    self_verified = 0;

    /* Find local player pawn via the matching Engine SDK layout:
     * UWorld+0x190 -> GI+0x38 -> LP[0]+0x30 -> PC+0x3f0 -> AcknowledgedPawn. */
    uint64_t local_pawn = 0;
    uint64_t local_pc = 0;
    uint64_t local_root = 0;
    uint64_t local_class = 0;
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
                                float position[3];
                                if (read_component_position(pawn_root, pos_off, position)) {
                                    local_root = pawn_root;
                                    for (int axis=0;axis<3;axis++) cached_self_pos[axis] = position[axis]/100.0f;
                                }
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
                "sdkCache(ts=%.3f loc=%.1f,%.1f,%.1f rot=%.1f,%.1f fov=%.1f) "
                "fppFov=%.1f\n",
                (unsigned long)local_pc, camera_manager_off,
                (unsigned long)camera_manager, read_float(cache),
                read_float(pov), read_float(pov + 4), read_float(pov + 8),
                read_float(pov + OFF_POV_ROTATION),
                read_float(pov + OFF_POV_ROTATION + 4),
                read_float(pov + OFF_POV_FOV),
                read_float(camera_manager + OFF_GP_CAMERA_FPP_FOV));
            camera_diag_logged = 1;
        }
        if (is_game_heap_ptr(camera_manager)) {
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
            camera_x = read_float(pov + OFF_POV_LOCATION) / 100.0f;
            camera_y = read_float(pov + OFF_POV_LOCATION + 4) / 100.0f;
            camera_z = read_float(pov + OFF_POV_LOCATION + 8) / 100.0f;
            camera_pitch = read_float(pov + OFF_POV_ROTATION);
            camera_yaw = read_float(pov + OFF_POV_ROTATION + 4);
            camera_fov = read_float(pov + OFF_POV_FOV);
            /* FMinimalViewInfo::FOV is the final rendered horizontal angle.
             * +0x20 is DesiredFOV; FPPCameraFOV is a derived setting, not it. */
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
    static uint8_t cached_actor_kinds[MAX_ACTORS]; /* 0=combat pawn, 1=loot/interactable */
    static uint8_t cached_actor_authoritative[MAX_ACTORS];
    static int cached_actor_count = 0;
    static unsigned cache_age = 0;
    static uint64_t battlefield_system = 0;
    static uint64_t battlefield_world = 0;
    static unsigned battlefield_attempt_tick = 0;
    if (battlefield_world != uworld) {
        battlefield_world = uworld;
        battlefield_system = 0;
        battlefield_attempt_tick = 0;
    }
    if (!is_game_heap_ptr(battlefield_system) && is_game_heap_ptr(local_pawn) &&
        (battlefield_attempt_tick++ % 300) == 0) {
        battlefield_system = find_battlefield_system(uworld, local_pc, local_pawn);
        if (!is_game_heap_ptr(battlefield_system) && compatibility_scan)
            battlefield_system = scan_battlefield_system(local_pc, local_pawn);
        if (is_game_heap_ptr(battlefield_system))
            fprintf(stderr, "\n[+] GPBattleFieldSystem=0x%lx (authoritative roster)\n",
                    (unsigned long)battlefield_system);
    }
    int refresh_cache = cached_uworld != uworld || cached_actor_data != actor_data ||
                        cached_actor_count == 0 || cache_age >= 20;
    if (refresh_cache) {
        uint64_t levels[2048] = {0};
        int level_count = 1;
        levels[0] = level;

        /* Remote human pawns may be owned by streamed levels while bots and
         * the local pawn remain in PersistentLevel. Walk UWorld::Levels and
         * merge their actor arrays into one deduplicated transform cache. */
        uint64_t levels_data = read_ptr(uworld + OFF_UWORLD_LEVELS);
        int32_t world_level_count = read_i32(uworld + OFF_UWORLD_LEVELS + 8);
        if (is_any_ptr(levels_data) && world_level_count > 0 && world_level_count <= 2047) {
            uint64_t streamed[2047] = {0};
            if (read_mem(levels_data, streamed, (size_t)world_level_count * 8) ==
                (ssize_t)((size_t)world_level_count * 8)) {
                for (int li = 0; li < world_level_count && level_count < 2048; li++) {
                    if (!is_any_ptr(streamed[li])) continue;
                    int duplicate = 0;
                    for (int existing = 0; existing < level_count; existing++)
                        if (levels[existing] == streamed[li]) { duplicate = 1; break; }
                    if (!duplicate) levels[level_count++] = streamed[li];
                }
            }
        }

        uint64_t *all_actor_ptrs = (uint64_t *)(read_buf + READ_BUF_SIZE / 2);
        uint64_t loot_actor_ptrs[MAX_ACTORS];
        uint8_t loot_actor_kinds[MAX_ACTORS];
        int loot_actor_count = 0;
        int death_box_count = 0;
        int scanned_actor_count = 0;
        uint64_t authoritative_characters[MAX_ACTORS] = {0};
        int authoritative_count = read_authoritative_characters(
            battlefield_system, local_pawn, authoritative_characters, MAX_ACTORS);
        cached_actor_count = 0;
        memset(cached_actor_authoritative, 0, sizeof(cached_actor_authoritative));
        for (int li = 0; li < level_count && cached_actor_count < MAX_ACTORS; li++) {
            uint64_t level_actor_data = 0;
            int32_t level_actor_count = 0;
            if (actor_source == 0) {
                uint64_t cluster = read_ptr(levels[li] + OFF_ULEVEL_ACTOR_CLUSTER);
                if (!is_any_ptr(cluster)) continue;
                level_actor_data = read_ptr(cluster + OFF_ACTOR_CONTAINER_ACTORS);
                level_actor_count = read_i32(cluster + OFF_ACTOR_CONTAINER_ACTORS + 8);
            } else {
                level_actor_data = read_ptr(levels[li] + actor_source);
                level_actor_count = read_i32(levels[li] + actor_source + 8);
            }
            if (!is_any_ptr(level_actor_data) || level_actor_count <= 0 || level_actor_count > 50000)
                continue;
            int scan_count = level_actor_count < 2000 ? level_actor_count : 2000;
            if (read_mem(level_actor_data, all_actor_ptrs, (size_t)scan_count * 8) !=
                (ssize_t)((size_t)scan_count * 8)) continue;

            for (int i = 0; i < scan_count && cached_actor_count < MAX_ACTORS; i++) {
                uint64_t actor = all_actor_ptrs[i];
                if (!is_any_ptr(actor)) continue;
                scanned_actor_count++;
                int already_cached = 0;
                for (int ci = 0; ci < cached_actor_count; ci++)
                    if (cached_actor_ptrs[ci] == actor) { already_cached = 1; break; }
                if (already_cached) continue;

                uint64_t cls = read_ptr(actor + OFF_UOBJECT_CLASS);
                int is_character = 0;
                if (authoritative_count > 0) {
                    for (int ai = 0; ai < authoritative_count; ai++)
                        if (authoritative_characters[ai] == actor) {
                            is_character = 1;
                            break;
                        }
                } else {
                    is_character = actor == local_pawn || is_character_class(cls, local_class);
                }

                /* Never infer loot solely from bytes inside a pawn-sized
                 * object.  Character and pickup subclasses have large,
                 * overlapping layouts in this title, so a character can
                 * accidentally satisfy the old death-box/pickup score.  A
                 * capsule root plus APawn ownership is independent structural
                 * evidence and takes precedence over all loot heuristics. */
                int looks_like_pawn = 0;
                uint64_t possible_root = read_ptr(actor + root_off);
                if (is_game_heap_ptr(possible_root)) {
                    float hh = read_float(possible_root +
                        OFF_CAPSULE_COMPONENT_HALF_HEIGHT) / 100.0f;
                    uint64_t state = read_ptr(actor + OFF_APAWN_PLAYER_STATE);
                    uint64_t controller = read_ptr(actor + OFF_APAWN_CONTROLLER);
                    if (isfinite(hh) && hh >= 0.4f && hh <= 3.0f &&
                        (is_game_heap_ptr(state) || is_game_heap_ptr(controller))) {
                        looks_like_pawn = 1;

                        /* A currently possessed pawn is authoritative even if
                         * the subsystem array is between replication updates. */
                        uint64_t active_pawn = is_game_heap_ptr(state) ?
                            read_ptr(state + OFF_PLAYER_STATE_PAWN_PRIVATE) : 0;
                        if (active_pawn == actor) is_character = 1;
                    }
                }

                int loot_kind = 0;
                if (!is_character && !looks_like_pawn) {
                    /* Reflected inheritance is definitive and cannot confuse a
                     * moving pawn with a pickup merely because their large
                     * derived layouts contain similar-looking bytes. */
                    loot_kind = loot_kind_from_class(cls);
                    if (!loot_kind && !fname_pool_addr && compatibility_scan)
                        loot_kind = classify_loot_actor(actor);
                }
                int is_loot = loot_kind != 0;

                /* Human operators do not always share the local pawn's concrete
                 * class hierarchy (different operator/skin blueprints). A named
                 * PlayerState is a fallback, but only if the actor also looks
                 * like a real pawn (has a CapsuleComponent root). Without
                 * this check, APlayerState actors pass the name test and
                 * produce ghost boxes at wrong positions. */
                if (authoritative_count == 0 && !is_character && !is_loot) {
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
                        if (is_game_heap_ptr(state)) {
                            char name[64] = {0};
                            read_fstring(state + OFF_PLAYER_STATE_NAME, name, sizeof(name));
                            if (!name[0])
                                read_fstring(state + OFF_PLAYER_STATE_NAME_PRIVATE, name, sizeof(name));
                            is_character = name[0] != '\0';
                        }
                    }
                }

                if (is_character) {
                    cached_actor_ptrs[cached_actor_count] = actor;
                    cached_actor_kinds[cached_actor_count] = 0;
                    cached_actor_authoritative[cached_actor_count] = authoritative_count > 0;
                    cached_actor_count++;

                    /* The health component owns the authoritative spawned
                     * death-box pointer. Following it catches AI boxes whose
                     * inherited pickup metadata is empty. */
                    HealthLayout *layout = health_layout_for_actor(actor);
                    int health_off = layout && layout->component_off >= 0 ?
                        layout->component_off : OFF_CHARACTER_HEALTH_COMPONENT;
                    uint64_t health_comp = read_ptr(actor + (uint64_t)health_off);
                    uint64_t death_box = is_game_heap_ptr(health_comp) ?
                        read_ptr(health_comp + 0x7E8) : 0;
                    if (is_game_heap_ptr(death_box) && loot_actor_count < MAX_ACTORS) {
                        int duplicate_box = 0;
                        for (int li2 = 0; li2 < loot_actor_count; li2++)
                            if (loot_actor_ptrs[li2] == death_box) { duplicate_box = 1; break; }
                        if (!duplicate_box) {
                            loot_actor_ptrs[loot_actor_count] = death_box;
                            loot_actor_kinds[loot_actor_count++] = 4;
                            death_box_count++;
                        }
                    }
                } else if (is_loot && loot_actor_count < MAX_ACTORS) {
                    int duplicate_loot = 0;
                    for (int li2 = 0; li2 < loot_actor_count; li2++)
                        if (loot_actor_ptrs[li2] == actor) { duplicate_loot = 1; break; }
                    if (!duplicate_loot) {
                        loot_actor_ptrs[loot_actor_count] = actor;
                        loot_actor_kinds[loot_actor_count++] = (uint8_t)loot_kind;
                        if (loot_kind == 4) death_box_count++;
                    }
                }
            }
        }
        /* A streamed character may be registered by the battlefield system
         * before its owning level's actor array is visible. Keep the game
         * roster complete even across that streaming window. */
        for (int ai = 0; ai < authoritative_count && cached_actor_count < MAX_ACTORS; ai++) {
            uint64_t actor = authoritative_characters[ai];
            if (!is_game_heap_ptr(actor)) continue;
            int duplicate = 0;
            for (int ci = 0; ci < cached_actor_count; ci++)
                if (cached_actor_ptrs[ci] == actor) { duplicate = 1; break; }
            if (!duplicate) {
                cached_actor_ptrs[cached_actor_count] = actor;
                cached_actor_kinds[cached_actor_count] = 0;
                cached_actor_authoritative[cached_actor_count] = 1;
                cached_actor_count++;
            }
        }
        /* Character pawns are latency-sensitive and take priority. Fill the
         * remainder with verified loot/interactable actors. */
        for (int i = 0; i < loot_actor_count && cached_actor_count < MAX_ACTORS; i++) {
            cached_actor_ptrs[cached_actor_count] = loot_actor_ptrs[i];
            int loot_kind = loot_actor_kinds[i] > 0 ? loot_actor_kinds[i] :
                classify_loot_actor(loot_actor_ptrs[i]);
            cached_actor_kinds[cached_actor_count] = loot_kind > 0 ? (uint8_t)loot_kind : 1;
            cached_actor_authoritative[cached_actor_count] = 0;
            cached_actor_count++;
        }
        static unsigned cache_diag_tick = 0;
        if ((cache_diag_tick++ % 30) == 0)
            fprintf(stderr, "\n[cache] scanned=%d authoritative=%d loot=%d deathBoxes=%d cached=%d levels=%d\n",
                    scanned_actor_count, authoritative_count, loot_actor_count, death_box_count,
                    cached_actor_count, level_count);
        cached_uworld = uworld;
        cached_actor_data = actor_data;
        cache_age = 0;
    } else {
        cache_age++;
        uint64_t current_characters[MAX_ACTORS];
        int current_count = read_authoritative_characters(
            battlefield_system, local_pawn, current_characters, MAX_ACTORS);
        cached_actor_count = update_cached_combatants(
            cached_actor_ptrs, cached_actor_kinds, cached_actor_authoritative,
            cached_actor_count, MAX_ACTORS, current_characters, current_count);
    }

    uint64_t *actor_ptrs = cached_actor_ptrs;
    int to_read = cached_actor_count;
    num_entities = 0;

    /* Collect positions for duplicate detection */
    float pos_list[MAX_ACTORS][3];
    int pos_count = 0;

    int player_num = 0;
    if (loot_values.base != ue4_base) {
        memset(&loot_values, 0, sizeof(loot_values));
        loot_values.base = ue4_base; loot_values.read = read_loot_value_memory;
    }
    lv_begin_frame(&loot_values);
    int diag_actor_ptr = 0;
    int diag_controller = 0;
    int diag_player_state = 0;
    int diag_root = 0;
    int diag_finite_position = 0;
    int diag_non_origin = 0;
    for (int i = 0; i < to_read && num_entities < MAX_ACTORS; i++) {
        if (!is_any_ptr(actor_ptrs[i])) continue;
        diag_actor_ptr++;
        int loot_kind = cached_actor_kinds[i];
        int is_loot_actor = loot_kind != 0;

        int is_self_pawn = (local_pawn != 0 && actor_ptrs[i] == local_pawn);
        diag_controller++;

        /* APawn::PlayerState is explicit in the matching UE4 SDK layout. The old
         * heuristic searched unrelated fields and mislabeled real pawns as loot. */
        uint64_t player_state = read_ptr(actor_ptrs[i] + OFF_APAWN_PLAYER_STATE);
        int has_playerstate = !is_loot_actor && is_game_heap_ptr(player_state);
        diag_player_state++;

        /* PlayerState::PawnPrivate is the authoritative currently possessed
         * pawn. Old respawn/streaming pawn objects can remain in ULevel for
         * minutes with plausible health and transforms far below the map. */
        if (has_playerstate && !cached_actor_authoritative[i]) {
            uint64_t active_pawn = read_ptr(player_state + OFF_PLAYER_STATE_PAWN_PRIVATE);
            if (is_game_heap_ptr(active_pawn) && active_pawn != actor_ptrs[i]) continue;
        }

        // Dead characters do not produce an overlay and need no emulation.
        if (!is_loot_actor && !is_self_pawn) {
            uint8_t actor_hidden=0, actor_dead=0, ps_dead=0;
            // AActor::bHidden is bit 0 at +0x90 in the matching SDK.
            // Extracted/inactive pawns can retain plausible cached health.
            read_mem(actor_ptrs[i]+0x90,&actor_hidden,1);
            if(actor_hidden&1)continue;
            read_mem(actor_ptrs[i]+OFF_CHARACTER_LAST_IS_DEAD,&actor_dead,1);
            if(has_playerstate)read_mem(player_state+OFF_PLAYER_STATE_DEAD,&ps_dead,1);
            if((actor_dead&1)||(ps_dead&1))continue;
        }
        uint64_t root = read_ptr(actor_ptrs[i] + root_off);
        if (!is_any_ptr(root)) continue;
        diag_root++;

        /* Keep remote enemy bounds and the verified loot transform separate. */
        float world_position[3];
        int position_valid = is_loot_actor ?
            read_component_position(root, pos_off, world_position) :
            read_character_position(root, world_position);
        if (!position_valid) continue;
        float wx=world_position[0], wy=world_position[1], wz=world_position[2];

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
        }
        char player_name[64] = {0};
        uint8_t bot_flags = 0;
        if (has_playerstate) {
            read_fstring(player_state + OFF_PLAYER_STATE_NAME, player_name, sizeof(player_name));
            if (!player_name[0])
                read_fstring(player_state + OFF_PLAYER_STATE_NAME_PRIVATE, player_name, sizeof(player_name));
            read_mem(player_state + OFF_PLAYER_STATE_BOT_FLAGS, &bot_flags, 1);
        }
        if (is_loot_actor) {
            if (loot_kind == 4) {
                char owner_name[48] = {0};
                read_fstring(actor_ptrs[i] + 0x2638, owner_name, sizeof(owner_name));
                snprintf(e->name, sizeof(e->name), "%s%s%s", "Death Box",
                         owner_name[0] ? ": " : "", owner_name);
            } else if (loot_kind == 3) {
                strncpy(e->name, "Stash / Interactable", sizeof(e->name) - 1);
            } else if (loot_kind == 2) {
                strncpy(e->name, "Loot Container", sizeof(e->name) - 1);
            } else {
                strncpy(e->name, "Loot Item", sizeof(e->name) - 1);
            }
        } else if (player_name[0]) {
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

        strncpy(e->kind, is_loot_actor ? "loot" :
            (has_playerstate && !(bot_flags & 0x8) ? "player" : "ai"), sizeof(e->kind) - 1);
        e->player_state = has_playerstate ? player_state : 0;
        e->native_team = -1;

        e->x = mx;
        e->y = my;
        e->z = mz;
        e->yaw = read_float(root + OFF_USCENE_RELATIVE_YAW);
        e->pitch = read_float(root + OFF_USCENE_RELATIVE_PITCH);
        /* Matching SDK chain:
         * AGPCharacterBase::HealthComp -> UGPHealthDataComponent::HealthSet ->
         * FGPGameplayAttributeData::{Health,MaxHealth}.CurrentValue. */
        e->health = 0.0f;
        e->max_health = 0.0f;
        e->health_known = 0;
        if (is_loot_actor) {
            e->dead = 0;
            e->half_height = 0.35f;
            e->team = 2;
            e->has_bones = 0;
            e->loot = lv_actor(&loot_values, actor_ptrs[i],
                              loot_metadata_kind(actor_ptrs[i], loot_kind), diag_now_ms());
            num_entities++;
            continue;
        }
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

        /* Discover skeleton mesh component on self pawn for bone reading */
        if (is_self_pawn && resolved_mesh_off == -2) {
            resolved_mesh_off = find_mesh_component(actor_ptrs[i]);
            if (resolved_mesh_off >= 0) {
                fprintf(stderr, "\n[+] SkeletalMeshComponent at actor+0x%x\n", resolved_mesh_off);
                uint64_t mesh_comp = read_ptr(actor_ptrs[i] + resolved_mesh_off);
                if (is_any_ptr(mesh_comp))
                    discover_bone_map(mesh_comp);
            } else {
                resolved_mesh_off = -1;
                fprintf(stderr, "\n[!] SkeletalMeshComponent not found on self pawn\n");
                /* Dump all UObject fields on the pawn to help find it */
                if (fname_pool_addr) {
                    fprintf(stderr, "[diag] UObject fields on self pawn:\n");
                    for (int off = 0x180; off < 0xC00; off += 8) {
                        uint64_t obj = read_ptr(actor_ptrs[i] + (uint64_t)off);
                        if (!is_game_heap_ptr(obj)) continue;
                        uint64_t ocls = read_ptr(obj + OFF_UOBJECT_CLASS);
                        if (!is_any_ptr(ocls)) continue;
                        char cn[96] = {0};
                        resolve_fname(read_i32(ocls + OFF_UOBJECT_NAME), cn, sizeof(cn));
                        if (cn[0])
                            fprintf(stderr, "  +0x%03x -> %s\n", off, cn);
                    }
                }
            }
        }

        /* Read bones for this entity */
        e->has_bones = 0;
        if (resolved_mesh_off > 0)
            read_entity_bones(e, actor_ptrs[i], root, resolved_mesh_off, pos_off);

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

        /* Team normalization happens after the complete live roster is known. */
        e->team = is_self_pawn ? 1 : 2;

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
            static int transform_diag_done = 0;
            if (!transform_diag_done) {
                transform_diag_done = 1;
                fprintf(stderr, "[diag] authoritative root transforms:\n");
                for (int ci = 0; ci < to_read && ci < 24; ci++) {
                    if (cached_actor_kinds[ci] != 0 || !cached_actor_authoritative[ci]) continue;
                    uint64_t actor = actor_ptrs[ci];
                    uint64_t root = read_ptr(actor + root_off);
                    if (!is_game_heap_ptr(root)) continue;
                    fprintf(stderr,
                        "  actor=0x%lx parent=0x%lx p148=(%.1f,%.1f,%.1f) "
                        "rel164=(%.1f,%.1f,%.1f) ctw1d0=(%.1f,%.1f,%.1f)\n",
                        (unsigned long)actor, (unsigned long)read_ptr(root + 0x108),
                        read_float(root + 0x148) / 100.0f,
                        read_float(root + 0x14C) / 100.0f,
                        read_float(root + 0x150) / 100.0f,
                        read_float(root + 0x164) / 100.0f,
                        read_float(root + 0x168) / 100.0f,
                        read_float(root + 0x16C) / 100.0f,
                        read_float(root + 0x1D0) / 100.0f,
                        read_float(root + 0x1D4) / 100.0f,
                        read_float(root + 0x1D8) / 100.0f);
                }
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

    /* Resolve and normalize native team IDs to the API's stable
     * 1=self-or-squad / 2=other model. Unknown players deliberately fail open
     * as opponents so a stale offset can never make distant humans disappear. */
    if (resolved_team_off < 0 && (team_resolve_retry++ % 90) == 0)
        resolved_team_off = resolve_team_field(self_entity_id);
    int self_native_team = -1;
    for (int i = 0; i < num_entities; i++) {
        entities[i].native_team = resolved_team_off >= 0 &&
            is_game_heap_ptr(entities[i].player_state) ?
            read_i32(entities[i].player_state + (uint64_t)resolved_team_off) : -1;
        if (strcmp(entities[i].id, self_entity_id) == 0) {
            self_native_team = entities[i].native_team;
        }
    }
    if (resolved_team_off >= 0) {
        int human_count = 0;
        int invalid_count = 0;
        for (int i = 0; i < num_entities; i++) {
            if (strcmp(entities[i].kind, "player") != 0) continue;
            human_count++;
            if (entities[i].native_team < 0 || entities[i].native_team > 1000000000)
                invalid_count++;
        }
        if (human_count >= 3 && invalid_count * 2 >= human_count) {
            fprintf(stderr,
                    "\n[!] Rejected unstable PlayerState team field +0x%x (%d/%d invalid)\n",
                    resolved_team_off, invalid_count, human_count);
            resolved_team_off = -1;
            team_resolve_retry = 0;
            self_native_team = -1;
            for (int i = 0; i < num_entities; i++) entities[i].native_team = -1;
        }
    }
    for (int i = 0; i < num_entities; i++) {
        if (strcmp(entities[i].id, self_entity_id) == 0) entities[i].team = 1;
        else if (self_native_team >= 0 && entities[i].native_team >= 0)
            entities[i].team = entities[i].native_team == self_native_team ? 1 : 2;
        else entities[i].team = 2;
    }
    {
        static unsigned team_diag_tick = 0;
        if ((team_diag_tick++ % 90) == 0) {
            fprintf(stderr, "\n[team] field=%s0x%x self=%d\n",
                    resolved_team_off >= 0 ? "+" : "", resolved_team_off,
                    self_native_team);
            for (int i = 0; i < num_entities; i++) {
                if (strcmp(entities[i].kind, "player") == 0)
                    fprintf(stderr, "  team=%d native=%d name=%s\n",
                            entities[i].team, entities[i].native_team, entities[i].name);
            }
        }
    }

    /* Fallback self: if pawn not matched, create synthetic self from PC data */
    if (self_entity_id[0] == 0 && local_pc && num_entities > 0 && num_entities < MAX_ACTORS) {
        Entity *e = &entities[num_entities];
        snprintf(e->id, sizeof(e->id), "self");
        strncpy(e->name, "Self", sizeof(e->name) - 1);
        strncpy(e->kind, "player", sizeof(e->kind) - 1);
        e->team = 1;
        e->native_team = -1;
        e->player_state = 0;
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
    /* Do not hold the camera collector for a second behind a slow receiver.
     * Partial/failed frames close this stream and the next scan reconnects. */
    struct timeval tv = {.tv_sec = 0, .tv_usec = 33000};
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

static int post_to_radar(void) {
    int sock = get_radar_sock();
    if (sock < 0) return -1;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long now_ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    clock_gettime(CLOCK_BOOTTIME, &ts);
    long long captured_elapsed_ms = (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

    static char body[MAX_ACTORS * 2048 + 4096];
    int off = 0;
    off += snprintf(body + off, sizeof(body) - off,
        "{\"self\":\"%s\",\"capturedElapsedMs\":%lld,\"map\":{\"name\":\"delta_force\",\"width\":10000,\"height\":10000},\"entities\":[",
        self_entity_id[0] ? self_entity_id : "", captured_elapsed_ms);
    for (int i = 0; i < num_entities; i++) {
        if (off > (int)sizeof(body) - 1536) break;
        if (i > 0) body[off++] = ',';
        char escaped_name[256];
        json_escape(entities[i].name, escaped_name, sizeof(escaped_name));
        off += snprintf(body + off, sizeof(body) - off,
            "{\"id\":\"%s\",\"kind\":\"%s\",\"x\":%.1f,\"y\":%.1f,\"z\":%.1f,"
            "\"yaw\":%.1f,\"pitch\":%.1f,\"fov\":%.1f,\"name\":\"%s\",\"health\":%.1f,\"maxHealth\":%.1f,"
            "\"healthKnown\":%s,"
            "\"halfHeight\":%.2f,\"dead\":%s,\"team\":%d,\"lastSeen\":%lld",
            entities[i].id, entities[i].kind[0] ? entities[i].kind : "player",
            entities[i].x, entities[i].y, entities[i].z,
            entities[i].yaw, entities[i].pitch, entities[i].fov, escaped_name, entities[i].health,
            entities[i].max_health, entities[i].health_known ? "true" : "false",
            entities[i].half_height, entities[i].dead ? "true" : "false",
            entities[i].team, now_ms);
        if (!strcmp(entities[i].kind, "loot")) {
            LootValue *loot = &entities[i].loot;
            off += snprintf(body + off, sizeof(body) - off,
                ",\"lootItemId\":\"%llu\",\"lootUnitValue\":%lld,\"lootValueKnown\":%s,"
                "\"lootContentsKnown\":%s,\"lootContainer\":%s,\"lootValueSource\":\"%s\"",
                (unsigned long long)loot->item_id, (long long)loot->unit_value,
                loot->value_known ? "true" : "false", loot->contents_known ? "true" : "false",
                loot->container ? "true" : "false", loot->value_known ? LV_PRICE_SOURCE : "unknown");
        }
        if (entities[i].has_bones) {
            off += snprintf(body + off, sizeof(body) - off, ",\"bones\":[");
            for (int b = 0; b < NUM_SKELETON_BONES; b++) {
                if (b > 0) body[off++] = ',';
                off += snprintf(body + off, sizeof(body) - off,
                    "[%.2f,%.2f,%.2f]",
                    entities[i].bone_pos[b][0],
                    entities[i].bone_pos[b][1],
                    entities[i].bone_pos[b][2]);
            }
            off += snprintf(body + off, sizeof(body) - off, "]");
        }
        body[off++] = '}';
    }
    off += snprintf(body + off, sizeof(body) - off, "]}");

    body[off++] = '\n';

    ssize_t sent = send(sock, body, off, MSG_NOSIGNAL);
    if (sent != off) {
        fprintf(stderr, "\n[!] post send failed: %s\n", strerror(errno));
        close_radar_sock();
        return -1;
    }
    return 0;
}

static void sig_handler(int sig) { (void)sig; running = 0; }

#define WORLD_CACHE_PATH "/data/local/tmp/delta-radar/world-api31.cache"

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
    if (argc == 2 && strcmp(argv[1], "--identity") == 0) {
        printf("{\"project\":\"delta-radar\",\"api\":%d,\"revision\":\"%s\",\"sourceSha256\":\"%s\"}\n",
               __ANDROID_API__, DELTA_SOURCE_REVISION, DELTA_SOURCE_SHA256);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "session=", 8) == 0) session_file = argv[i] + 8;
        else if (strncmp(argv[i], "owner=", 6) == 0) session_owner_pid = (pid_t)strtol(argv[i] + 6, NULL, 10);
    }
    if (!session_active()) { fprintf(stderr, "Session cancelled before startup\n"); return 125; }
    fprintf(stderr, "DeltaRadar scanner API=%d revision=%s source=%s\n",
            __ANDROID_API__, DELTA_SOURCE_REVISION, DELTA_SOURCE_SHA256);
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
        else if (strcmp(argv[i], "compat-scan") == 0) compatibility_scan = 1;
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

    /* This build's pool is known. Never search the whole heap for a plaintext
     * sentinel: Delta Force encodes its names and that search selects unrelated
     * text buffers. Unreadable data remains unavailable. */
    fname_pool_addr = ue4_base + OFF_GNAMES_FROM_UE4_BASE;
    char pool_name[16] = {0};
    if (resolve_fname(0, pool_name, sizeof(pool_name)) && !strcmp(pool_name, "None"))
        fprintf(stderr, "[+] SDK FNamePool validated at 0x%lx\n", (unsigned long)fname_pool_addr);
    else {
        fname_pool_addr = 0;
        fprintf(stderr, "[!] SDK name pool unavailable; no compatibility heap scan\n");
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
                    for (int wait = 0; wait < 30 && session_active(); wait++) usleep(100000);
                    if (!session_active()) break;
                    int latest_pid = find_game_pid();
                    if (latest_pid > 0 && latest_pid != game_pid) {
                        fprintf(stderr, "[+] Game restarted while waiting; reattaching\n");
                        execv(argv[0], argv);
                        break;
                    }
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
        while (session_active()) {
            if ((pid_tick++ % 30) == 0) {
                int active_pid = find_game_pid();
                if (active_pid > 0 && active_pid != game_pid) {
                    fprintf(stderr, "\n[+] Game PID changed %d -> %d; reattaching\n",
                            game_pid, active_pid);
                    num_entities = 0;
                    self_entity_id[0] = 0;
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
            /* A ULevel can briefly rebuild its actor array during streaming.
             * Recover through another known container instead of blocking the
             * feed on a process-wide GWorld scan. */
            if (n < 0) {
                uint64_t current_world = read_ptr(use_gworld);
                uint64_t current_level = is_any_ptr(current_world) ?
                    read_ptr(current_world + OFF_UWORLD_PERSISTENT_LEVEL) : 0;
                if (is_any_ptr(current_world) && is_any_ptr(current_level)) {
                    uint8_t lvl_buf[ULEVEL_SCAN_END];
                    if (read_mem(current_level, lvl_buf, sizeof(lvl_buf)) == (ssize_t)sizeof(lvl_buf)) {
                        for (int off = ULEVEL_SCAN_START;
                             off < ULEVEL_SCAN_END && n < 0;
                             off += ULEVEL_SCAN_STEP) {
                            if (off == use_actor_src) continue;
                            uint64_t aptr; int32_t acnt, acap;
                            memcpy(&aptr, lvl_buf + off, 8);
                            memcpy(&acnt, lvl_buf + off + 8, 4);
                            memcpy(&acap, lvl_buf + off + 12, 4);
                            if (!is_any_ptr(aptr) || acnt < 5 || acnt > 50000 || acap < acnt) continue;
                            int candidate_n = read_entities_sdk(use_gworld, off,
                                use_root_off, use_pos_off, use_ctw);
                            if (candidate_n >= 0) {
                                use_actor_src = off;
                                save_world_cache(game_pid, use_gworld, use_actor_src);
                                fprintf(stderr,
                                    "\n[+] Recovered actor array at level+0x%x without world scan\n",
                                    off);
                                n = candidate_n;
                            }
                        }
                    }
                }
            }
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
                num_entities = 0;
                self_entity_id[0] = 0;
                self_verified = 0;
                post_to_radar();
            }
            uint64_t selected_world = read_ptr(use_gworld);
            uint64_t selected_level = is_any_ptr(selected_world) ?
                read_ptr(selected_world + OFF_UWORLD_PERSISTENT_LEVEL) : 0;
            int selected_world_invalid = !is_any_ptr(selected_world) ||
                                         !is_any_ptr(selected_level);
            if (selected_world_invalid && is_any_ptr(verified_uworld)) {
                /* process_vm_readv can transiently miss one word while UE is
                 * updating mappings. A cheap second read avoids even the
                 * bounded alias search in that common case. */
                uint64_t retry_world = read_ptr(use_gworld);
                uint64_t retry_level = is_any_ptr(retry_world) ?
                    read_ptr(retry_world + OFF_UWORLD_PERSISTENT_LEVEL) : 0;
                if (is_any_ptr(retry_world) && is_any_ptr(retry_level))
                    selected_world_invalid = 0;
            }
            if (selected_world_invalid && is_any_ptr(verified_uworld)) {
                uint64_t recovered_addr = 0;
                int recovered_source = use_actor_src;
                if (recover_nearby_world_alias(use_gworld, verified_uworld,
                                               &recovered_addr, &recovered_source)) {
                    fprintf(stderr,
                        "\n[+] Failed over GWorld alias 0x%lx -> 0x%lx without full scan\n",
                        (unsigned long)use_gworld, (unsigned long)recovered_addr);
                    use_gworld = recovered_addr;
                    use_actor_src = recovered_source;
                    save_world_cache(game_pid, use_gworld, use_actor_src);
                    selected_world_invalid = 0;
                    consecutive_failures = 0;
                }
            }
            /* Empty/loading worlds update through the same global pointer.
             * Rediscover only after the selected world itself is invalid. */
            int need_rescan = selected_world_invalid && consecutive_failures > 45;
            if (need_rescan) {
                int active_pid = find_game_pid();
                if (active_pid > 0 && active_pid != game_pid) {
                    fprintf(stderr, "\n[+] Game PID changed %d -> %d before rescan; reattaching\n",
                            game_pid, active_pid);
                    num_entities = 0;
                    self_entity_id[0] = 0;
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
