/* Game-thread visibility bridge for the 1.202.37119.3601 SDK profile.
 * The scanner publishes value-only requests. Physics is invoked only after
 * an outermost Actor::ProcessEvent returns on the named UE GameThread.
 * Missing, old, moved, or mismatched observations never certify visibility.
 */
#ifndef ENGINE_VISIBILITY_H
#define ENGINE_VISIBILITY_H
#include <stddef.h>
#include "aim_math.h"

#define EV_MAX 128
#define EV_SAMPLES 5
#define EV_MAX_AGE_MS 100
#define EV_PE_SLOT 0x45
#define EV_ACTOR_PE_RVA 0x107e2070
#define EV_OBJECT_PE_RVA 0x107e4460

typedef struct { uint64_t data; int32_t count, capacity; } EVArray;
typedef struct {
    uint64_t world;
    unsigned char hit[0x88];
    float start[3], end[3];
    uint8_t channel, complex;
    uint8_t padding[6];
    EVArray ignore;
    uint8_t result, tail[7];
} EVTraceParams;
_Static_assert(sizeof(EVTraceParams) == 0xc8, "trace parameter size");
_Static_assert(offsetof(EVTraceParams, start) == 0x90, "trace start");
_Static_assert(offsetof(EVTraceParams, channel) == 0xa8, "trace channel");
_Static_assert(offsetof(EVTraceParams, ignore) == 0xb0, "trace ignore list");
_Static_assert(offsetof(EVTraceParams, result) == 0xc0, "trace return");

typedef struct {
    uint64_t address, cls, name;
    int32_t index, serial;
} EVObject;
typedef struct {
    EVObject actor;
    float center[3], half_height;
    float targets[EV_SAMPLES][3]; /* centimetres */
    int visible;
    uint8_t blocked;
    int aim_eligible, aim_clear;
    float aim_point[3];
} EVJob;
typedef struct {
    uint64_t sequence;
    int64_t at_ms;
    EVObject world, controller, pawn;
    float camera[3]; /* centimetres */
    float yaw, pitch;
    uint32_t aim_config;
    int count;
    EVJob jobs[EV_MAX];
} EVFrame;

static pthread_mutex_t ev_mutex = PTHREAD_MUTEX_INITIALIZER;
static EVFrame ev_request, ev_result;
static EVObject ev_function, ev_receiver;
static _Atomic int ev_ready, ev_game_tid;
static _Atomic unsigned ev_callbacks, ev_batches, ev_traces;
static uint64_t ev_sequence;
static __thread int ev_depth;
static __thread SelfMemoryReader ev_reader = {-1, -1};
typedef void (*EVProcessEvent)(void *, void *, void *);
static EVProcessEvent ev_original, ev_dispatch;

/* Separate reader and diagnostics from the worker's mutable scanner state. */
static int ev_read(uint64_t address, void *out, size_t size) {
    if (address < 0x10000 || address > 0xffffffffffffULL - size) return 0;
    if (ev_reader.read_fd < 0 && self_memory_open(&ev_reader) < 0) return 0;
    return self_memory_read(&ev_reader, address, out, size) == (ssize_t)size;
}
static uint64_t ev_ptr(uint64_t address) {
    uint64_t value = 0; ev_read(address, &value, sizeof(value)); return value;
}
static int ev_item(int32_t index, uint64_t *item) {
    uint64_t array = ue4_base + OFF_GUOBJECTARRAY_FROM_BASE;
    int32_t count = 0;
    if (!ev_read(array + 0x14, &count, 4) || index < 0 || index >= count || count > 4000000) return 0;
    uint64_t chunks = ev_ptr(array + 0x20);
    uint64_t chunk = chunks ? ev_ptr(chunks + (uint64_t)(index / 65536) * 8) : 0;
    if (!chunk) return 0;
    *item = chunk + (uint64_t)(index % 65536) * 0x18;
    return 1;
}
static int ev_object(uint64_t address, EVObject *out) {
    unsigned char header[0x28], item_bytes[0x18];
    uint64_t item;
    memset(out, 0, sizeof(*out));
    if (!ev_read(address, header, sizeof(header))) return 0;
    uint32_t flags; memcpy(&flags, header + 0x18, 4);
    if (flags & (0x8000u | 0x10000u)) return 0; /* destruction in progress */
    memcpy(&out->index, header + 0x24, 4);
    if (!ev_item(out->index, &item) || !ev_read(item, item_bytes, sizeof(item_bytes))) return 0;
    uint64_t actual; uint32_t internal;
    memcpy(&actual, item_bytes, 8); memcpy(&internal, item_bytes + 8, 4);
    if (actual != address || (internal & 0x30000000u)) return 0;
    out->address = address;
    memcpy(&out->cls, header + 8, 8); memcpy(&out->name, header + 0x1c, 8);
    memcpy(&out->serial, item_bytes + 0x10, 4);
    return out->cls != 0;
}
static int ev_same(const EVObject *expected) {
    EVObject now;
    return expected->address && ev_object(expected->address, &now) &&
        now.cls == expected->cls && now.name == expected->name &&
        now.index == expected->index && now.serial == expected->serial;
}
static int64_t ev_now(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int ev_frame_fresh(const EVFrame *result, const EVFrame *request, int64_t now) {
    int64_t age = now - result->at_ms;
    if (!result->sequence || !request->world.address || age < 0 || age > EV_MAX_AGE_MS ||
        result->world.address != request->world.address ||
        result->controller.address != request->controller.address ||
        result->pawn.address != request->pawn.address || !ev_same(&result->world)) return 0;
    float camera_error = 0;
    for (int k = 0; k < 3; k++) {
        float d = result->camera[k] - request->camera[k];
        camera_error += d*d;
    }
    return isfinite(camera_error) && camera_error <= 25.0f * 25.0f;
}

/* Worker-only lookup. The hint is validated by names, outer, type and CDO;
 * a bounded GObjects walk handles different registration order. */
static int ev_bind_class(uint64_t cls) {
    char name[80], type[80];
    if (!is_any_ptr(cls)) return 0;
    resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, sizeof(name));
    resolve_fname(read_i32(read_ptr(cls + 8) + OFF_UOBJECT_NAME), type, sizeof(type));
    if (strcmp(name, "GPPhysics") || strcmp(type, "Class")) return 0;
    uint64_t receiver = read_ptr(cls + 0x138);
    if (read_ptr(receiver + 8) != cls) return 0;
    for (uint64_t field = read_ptr(cls + 0x50), n = 0; field && n < 512; n++, field = read_ptr(field + 0x28)) {
        resolve_fname(read_i32(field + OFF_UOBJECT_NAME), name, sizeof(name));
        if (strcmp(name, "LineTraceSingle") || read_ptr(field + 0x10) != cls) continue;
        uint16_t size = 0, return_offset = 0;
        uint32_t flags = 0;
        if (!ev_read(field + 0xb2, &size, 2) || size != offsetof(EVTraceParams, result) + 1 ||
            !ev_read(field + 0xb4, &return_offset, 2) || return_offset != 0xc0 ||
            !ev_read(field + 0xb8, &flags, 4) || !(flags & 0x400)) return 0;
        if (read_ptr(read_ptr(receiver) + EV_PE_SLOT * 8) != ue4_base + EV_OBJECT_PE_RVA) return 0;
        if (!ev_object(field, &ev_function) || !ev_object(receiver, &ev_receiver)) return 0;
        return 1;
    }
    return 0;
}
static int ev_bind(void) {
    uint64_t item;
    if (ev_item(0xd72, &item) && ev_bind_class(ev_ptr(item))) return 1;
    int32_t count = read_i32(ue4_base + OFF_GUOBJECTARRAY_FROM_BASE + 0x14);
    if (count <= 0 || count > 1000000) return 0;
    for (int32_t i = 0; i < count; i++) {
        if (ev_item(i, &item) && ev_bind_class(ev_ptr(item))) return 1;
    }
    return 0;
}

#include "engine_aim.h"

static void ev_execute(void) {
    /* This storage is accessed only by the game thread, with re-entry excluded. */
    static EVFrame frame;
    static uint64_t consumed;
    if (pthread_mutex_trylock(&ev_mutex)) return;
    if (!ev_request.sequence || ev_request.sequence == consumed) {
        pthread_mutex_unlock(&ev_mutex); return;
    }
    frame = ev_request; consumed = frame.sequence;
    pthread_mutex_unlock(&ev_mutex);
    int64_t age = ev_now() - frame.at_ms;
    if (age < 0 || age > EV_MAX_AGE_MS || !ev_same(&frame.world) ||
        !ev_same(&frame.controller) || !ev_same(&frame.pawn) ||
        !ev_same(&ev_function) || !ev_same(&ev_receiver)) return;
    /* Reconfirm the local world/possession at execution, not just enqueue. */
    uint64_t gi = ev_ptr(frame.world.address + 0x190);
    uint64_t locals = ev_ptr(gi + 0x38), local = ev_ptr(locals);
    if (ev_ptr(local + 0x30) != frame.controller.address ||
        ev_ptr(frame.controller.address + 0x3f0) != frame.pawn.address) return;
    EVProcessEvent dispatch = ev_dispatch;
    if (!dispatch) return;
    uint64_t ignored[2] = {frame.pawn.address, 0};
    for (int i = 0; i < frame.count; i++) {
        EVJob *job = &frame.jobs[i]; job->visible = -1; job->blocked = 0;
        if (!ev_same(&job->actor)) continue;
        ignored[1] = job->actor.address;
        /* The target is ignored so a body/capsule hit cannot make an exposed
         * sample red. Cover before the endpoint still blocks the sample. */
        for (int r = 0; r < EV_SAMPLES; r++) {
            EVTraceParams params = {0};
            params.world = frame.world.address;
            memcpy(params.start, frame.camera, sizeof(params.start));
            memcpy(params.end, job->targets[r], sizeof(params.end));
            params.channel = 3; /* matching EECollisionChannel::ECC_Visibility */
            params.complex = 1;
            params.ignore = (EVArray){(uint64_t)(uintptr_t)ignored, 2, 2};
            params.result = 0xff; /* Distinguish a dispatch that did not execute. */
            dispatch((void *)(uintptr_t)ev_receiver.address, (void *)(uintptr_t)ev_function.address, &params);
            atomic_fetch_add(&ev_traces, 1);
            if (params.result > 1) { job->visible = -1; break; }
            if (params.result) job->blocked |= (uint8_t)(1u << r);
            job->visible = job->blocked == (1u << EV_SAMPLES) - 1u ? 0 : 1;
        }
        job->aim_clear = 0;
        if ((frame.aim_config & 1) && job->aim_eligible && job->visible == 1) {
            EVTraceParams params = {0};
            params.world = frame.world.address;
            memcpy(params.start, frame.camera, sizeof(params.start));
            memcpy(params.end, job->aim_point, sizeof(params.end));
            params.channel = 3; params.complex = 1;
            params.ignore = (EVArray){(uint64_t)(uintptr_t)ignored, 2, 2};
            params.result = 0xff;
            dispatch((void *)(uintptr_t)ev_receiver.address, (void *)(uintptr_t)ev_function.address, &params);
            atomic_fetch_add(&ev_traces, 1);
            if (params.result == 0) job->aim_clear = 1;
            else if (params.result == 1) {
                /* Keep the aim on an exposed part instead of steering into
                 * cover when only a different body sample is visible. */
                const int preference[5] = {0,1,3,4,2};
                for (int k=0;k<5;k++) if (!(job->blocked & (1u << preference[k]))) {
                    memcpy(job->aim_point, job->targets[preference[k]], sizeof(job->aim_point));
                    job->aim_clear = 1; break;
                }
            }
        }
    }
    ea_frame(&frame);
    atomic_fetch_add(&ev_batches, 1);
    /* Never stall the host to publish diagnostics/visibility. */
    if (!pthread_mutex_trylock(&ev_mutex)) {
        ev_result = frame;
        pthread_mutex_unlock(&ev_mutex);
    }
}

static void ev_process_event(void *object, void *function, void *params) {
    ++ev_depth;
    ev_original(object, function, params);
    if (ev_depth == 1 && (int)syscall(__NR_gettid) == atomic_load(&ev_game_tid)) {
        if (atomic_load(&ev_ready) && atomic_load(&lib_running_flag)) {
            atomic_fetch_add(&ev_callbacks, 1);
            ev_execute();
        }
        ea_tick();
    }
    --ev_depth;
}

/* Native class vtables live for the engine image's lifetime. Change a single
 * verified Actor::ProcessEvent slot, preserving the page's exact permissions.
 * No object-owned vtable allocation or dangling replacement table is used. */
static int ev_hook(uint64_t object) {
    uint64_t table = read_ptr(object), slot = table + EV_PE_SLOT * 8;
    uint64_t value = read_ptr(slot);
    if (value == (uint64_t)(uintptr_t)ev_process_event) return 1;
    if (!is_in_ue4(table) || value != ue4_base + EV_ACTOR_PE_RVA) return 0;
    int protection = 0;
    for (int i = 0; i < num_regions; i++) if (slot >= regions[i].start && slot + 8 <= regions[i].end) {
        if (regions[i].perms[0] == 'r') protection |= PROT_READ;
        if (regions[i].perms[1] == 'w') protection |= PROT_WRITE;
        if (regions[i].perms[2] == 'x') protection |= PROT_EXEC;
        break;
    }
    if (!(protection & PROT_READ) || (protection & PROT_EXEC)) return 0;
    long page_size = sysconf(_SC_PAGESIZE);
    uintptr_t page = slot & ~((uintptr_t)page_size - 1);
    if (mprotect((void *)page, page_size, protection | PROT_WRITE)) return 0;
    uint64_t expected = value;
    int changed = __atomic_compare_exchange_n((uint64_t *)(uintptr_t)slot, &expected,
        (uint64_t)(uintptr_t)ev_process_event, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    int restored = mprotect((void *)page, page_size, protection) == 0;
    return changed && restored;
}
static int ev_find_game_tid(void) {
    DIR *directory = opendir("/proc/self/task");
    if (!directory) return 0;
    struct dirent *entry; int tid = 0;
    while ((entry = readdir(directory))) {
        if (atoi(entry->d_name) <= 0) continue;
        char path[96], name[32] = {0};
        snprintf(path, sizeof(path), "/proc/self/task/%s/comm", entry->d_name);
        FILE *file = fopen(path, "r");
        if (!file) continue;
        fgets(name, sizeof(name), file); fclose(file);
        if (!strcmp(name, "GameThread\n") && (!tid || atoi(entry->d_name) < tid)) tid = atoi(entry->d_name);
    }
    closedir(directory); return tid;
}
static int ev_initialize(void) {
    /* Both dispatch entries are from this exact SDK/native image, not probes. */
    const uint32_t actor_signature[] = {0xd101c3ff, 0xa9067bfd, 0x910183fd, 0x3942e428};
    const uint32_t object_signature[] = {0xa9ba6ffc, 0xa90167fa, 0xa9025ff8, 0xa90357f6};
    uint32_t actual[4];
    if (!ev_read(ue4_base + EV_ACTOR_PE_RVA, actual, sizeof(actual)) ||
        memcmp(actual, actor_signature, sizeof(actual)) ||
        !ev_read(ue4_base + EV_OBJECT_PE_RVA, actual, sizeof(actual)) ||
        memcmp(actual, object_signature, sizeof(actual))) return 0;
    int tid = ev_find_game_tid();
    if (!tid || !ev_bind()) return 0;
    ev_original = (EVProcessEvent)(uintptr_t)(ue4_base + EV_ACTOR_PE_RVA);
    ev_dispatch = (EVProcessEvent)(uintptr_t)(ue4_base + EV_OBJECT_PE_RVA);
    atomic_store(&ev_game_tid, tid);
    atomic_store(&ev_ready, 1);
    return 1;
}

static void ev_update(int count) {
    static EVFrame request, result;
    request = (EVFrame){0};
    request.sequence = ++ev_sequence;
    request.at_ms = ev_now();
    uint64_t world = diagnostic_world;
    uint64_t gi = read_ptr(world + 0x190), local = read_ptr(read_ptr(gi + 0x38));
    uint64_t pc = read_ptr(local + 0x30), pawn = read_ptr(pc + 0x3f0);
    int valid = count > 0 && camera_frame_valid && atomic_load(&ev_ready) &&
        ev_object(world, &request.world) && ev_object(pc, &request.controller) &&
        ev_object(pawn, &request.pawn);
    for (int i = 0; i < count; i++) {
        entities[i].visibility_known = 0;
        entities[i].visible_from_camera = 0;
        entities[i].engine_visible = -1;
        entities[i].visibility_ray_mask = 0;
    }
    Entity *self = NULL;
    for (int i = 0; i < count; i++) if (!strcmp(entities[i].id, self_entity_id)) self = &entities[i];
    valid = valid && self;
    if (valid) {
        request.camera[0] = self->x * 100; request.camera[1] = self->y * 100; request.camera[2] = self->z * 100;
        request.yaw = self->yaw; request.pitch = self->pitch;
        request.aim_config = atomic_load(&ea_config);
        if (self->dead || !local_player_controlled) request.aim_config = 0;
        if (request.aim_config & 1) ea_bind_controller(pc);
        ev_hook(pc); ev_hook(pawn); ev_hook(read_ptr(pc + 0x400));
        for (int i = 0; i < count && request.count < EV_MAX; i++) {
            Entity *entity = &entities[i];
            if (entity == self || entity->dead || !entity->position_plaintext) continue;
            EVJob *job = &request.jobs[request.count];
            if (!ev_object(entity->actor_address, &job->actor)) continue;
            job->center[0] = entity->x; job->center[1] = entity->y; job->center[2] = entity->z;
            job->half_height = entity->half_height;
            unsigned mask = (request.aim_config >> 1) & 5;
            int allowed = entity->kind[0] == 'a' ? (mask & 4) != 0 :
                entity->kind[0] == 'p' && entity->team != 1 && (mask & 1) != 0;
            job->aim_eligible = allowed && entity->health_known && isfinite(entity->health) && entity->health > 0.01f;
            int bone = (request.aim_config >> 16) & 3;
            float height = bone == 0 ? 0.8f : bone == 1 ? 0.45f : 0.2f;
            job->aim_point[0] = entity->x * 100;
            job->aim_point[1] = entity->y * 100;
            job->aim_point[2] = (entity->z + entity->half_height * height) * 100;
            for (int r = 0; r < EV_SAMPLES; r++) {
                visibility_sample_point(self->x, self->y, entity->x, entity->y, entity->z, entity->half_height, r, job->targets[r]);
                for (int k = 0; k < 3; k++) job->targets[r][k] *= 100;
            }
            job->visible = -1;
            request.count++;
        }
    }
    if (pthread_mutex_trylock(&ev_mutex)) return;
    result = ev_result;
    ev_request = request;
    pthread_mutex_unlock(&ev_mutex);
    if (!valid || !ev_frame_fresh(&result, &request, ev_now())) return;
    for (int i = 0; i < count; i++) {
        Entity *entity = &entities[i];
        for (int j = 0; j < result.count; j++) {
            EVJob *job = &result.jobs[j];
            if (job->actor.address != entity->actor_address || job->visible < 0 || !ev_same(&job->actor)) continue;
            float dx = entity->x - job->center[0], dy = entity->y - job->center[1], dz = entity->z - job->center[2];
            float movement = dx*dx + dy*dy + dz*dz;
            if (!isfinite(movement) || movement > 0.1f*0.1f ||
                !isfinite(entity->half_height) || fabsf(entity->half_height - job->half_height) > 0.05f) continue;
            entity->visibility_known = 1;
            entity->visible_from_camera = job->visible;
            entity->engine_visible = job->visible;
            entity->visibility_ray_mask = job->blocked;
            break;
        }
    }
}
#endif
