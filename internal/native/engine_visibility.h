/* Engine-level visibility via direct ProcessEvent calls from the scanner thread.
 * No vtable hooks, no mprotect on game pages, no game-thread interception.
 * PhysX read queries are thread-safe (shared lock during scene step). */
#ifndef ENGINE_VISIBILITY_H
#define ENGINE_VISIBILITY_H
#include <stddef.h>
#include "aim_math.h"

#define EV_MAX 128
#define EV_SAMPLES 5

/* ProcessEvent RVA verified by instruction signature before use. */
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

static EVObject ev_function, ev_receiver;
static _Atomic int ev_ready;
static _Atomic unsigned ev_batches, ev_traces;
static _Atomic uint64_t ev_local_pawn;
typedef void (*EVProcessEvent)(void *, void *, void *);
static EVProcessEvent ev_pe_fn;

/* Separate reader from the scanner's mutable state. */
static __thread SelfMemoryReader ev_reader = {-1, -1};
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
    if (flags & (0x8000u | 0x10000u)) return 0;
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

/* Bind the UGPPhysics CDO and LineTraceSingle UFunction via GObjects. */
__attribute__((noinline))
static void ev_decode(char *dst, const unsigned char *src, int n) {
    volatile unsigned char k = 0x55;
    for (int i = 0; i < n; i++) dst[i] = src[i] ^ k;
    dst[n] = 0;
}
static int ev_bind_class(uint64_t cls) {
    char name[80], type[80];
    if (!is_any_ptr(cls)) return 0;
    resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, sizeof(name));
    resolve_fname(read_i32(read_ptr(cls + 8) + OFF_UOBJECT_NAME), type, sizeof(type));
    char want_name[16], want_type[8];
    static const unsigned char en[] = {0x12,0x05,0x05,0x3d,0x2c,0x26,0x3c,0x36,0x26};
    static const unsigned char et[] = {0x16,0x39,0x34,0x26,0x26};
    ev_decode(want_name, en, 9);
    ev_decode(want_type, et, 5);
    if (strcmp(name, want_name) || strcmp(type, want_type)) return 0;
    uint64_t receiver = read_ptr(cls + 0x138);
    if (read_ptr(receiver + 8) != cls) return 0;
    char want_fn[20];
    static const unsigned char ef[] = {0x19,0x3c,0x3b,0x30,0x01,0x27,0x34,0x36,0x30,0x06,0x3c,0x3b,0x32,0x39,0x30};
    ev_decode(want_fn, ef, 15);
    for (uint64_t field = read_ptr(cls + 0x50), n = 0; field && n < 512; n++, field = read_ptr(field + 0x28)) {
        resolve_fname(read_i32(field + OFF_UOBJECT_NAME), name, sizeof(name));
        if (strcmp(name, want_fn) || read_ptr(field + 0x10) != cls) continue;
        uint16_t size = 0, return_offset = 0;
        uint32_t flags = 0;
        if (!ev_read(field + 0xb2, &size, 2) || size != offsetof(EVTraceParams, result) + 1 ||
            !ev_read(field + 0xb4, &return_offset, 2) || return_offset != 0xc0 ||
            !ev_read(field + 0xb8, &flags, 4) || !(flags & 0x400)) return 0;
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

/* Verify ProcessEvent signature and bind. No hooks, no mprotect on UE4 pages. */
static int ev_initialize(void) {
    const uint32_t object_signature[] = {0xa9ba6ffc, 0xa90167fa, 0xa9025ff8, 0xa90357f6};
    uint32_t actual[4];
    if (!ev_read(ue4_base + EV_OBJECT_PE_RVA, actual, sizeof(actual)) ||
        memcmp(actual, object_signature, sizeof(actual))) return 0;
    if (!ev_bind()) return 0;
    ev_pe_fn = (EVProcessEvent)(uintptr_t)(ue4_base + EV_OBJECT_PE_RVA);
    atomic_store(&ev_ready, 1);
    return 1;
}

/* Execute physics traces synchronously on the calling (scanner) thread. */
static void ev_execute_traces(int count, float cam_x, float cam_y, float cam_z,
                              uint64_t world_addr, uint64_t pawn_addr) {
    if (!ev_pe_fn || !ev_same(&ev_function) || !ev_same(&ev_receiver)) return;

    for (int i = 0; i < count; i++) {
        Entity *e = &entities[i];
        if (!e->visibility_known && !e->visible_from_camera) continue;
        if (!strcmp(e->id, self_entity_id) || e->dead) continue;
        if (!e->position_plaintext && (e->x == 0 && e->y == 0 && e->z == 0)) continue;
        if (!e->actor_address) continue;

        uint64_t ignored[2] = {pawn_addr, e->actor_address};
        uint8_t blocked = 0;
        int valid = 1;

        for (int r = 0; r < EV_SAMPLES && valid; r++) {
            float sample[3];
            visibility_sample_point(cam_x, cam_y, e->x, e->y, e->z, e->half_height, r, sample);

            EVTraceParams params = {0};
            params.world = world_addr;
            params.start[0] = cam_x * 100.0f;
            params.start[1] = cam_y * 100.0f;
            params.start[2] = cam_z * 100.0f;
            params.end[0] = sample[0] * 100.0f;
            params.end[1] = sample[1] * 100.0f;
            params.end[2] = sample[2] * 100.0f;
            params.channel = 3; /* ECC_Visibility */
            params.complex = 1;
            params.ignore = (EVArray){(uint64_t)(uintptr_t)ignored, 2, 2};
            params.result = 0xff;

            ev_pe_fn((void *)(uintptr_t)ev_receiver.address,
                     (void *)(uintptr_t)ev_function.address, &params);
            atomic_fetch_add(&ev_traces, 1);

            if (params.result > 1) { valid = 0; break; }
            if (params.result) blocked |= (uint8_t)(1u << r);
        }

        if (valid) {
            e->visibility_known = 1;
            e->visible_from_camera = (blocked == ((1u << EV_SAMPLES) - 1u)) ? 0 : 1;
            e->engine_visible = e->visible_from_camera;
            e->visibility_ray_mask = blocked;
        }
    }
    atomic_fetch_add(&ev_batches, 1);
}

static void ev_update(int count) {
    atomic_store(&ev_local_pawn, 0);
    for (int i = 0; i < count; i++) {
        entities[i].visibility_known = 0;
        entities[i].visible_from_camera = 0;
        entities[i].engine_visible = -1;
        entities[i].visibility_ray_mask = 0;
    }
    if (!atomic_load(&ev_ready) || !camera_frame_valid || count <= 0) return;

    uint64_t world = diagnostic_world;
    uint64_t gi = read_ptr(world + 0x190), local = read_ptr(read_ptr(gi + 0x38));
    uint64_t pc = read_ptr(local + 0x30), pawn = read_ptr(pc + 0x3f0);
    if (!is_any_ptr(world) || !is_any_ptr(pawn)) return;
    atomic_store(&ev_local_pawn, pawn);

    Entity *self = NULL;
    for (int i = 0; i < count; i++)
        if (!strcmp(entities[i].id, self_entity_id)) { self = &entities[i]; break; }
    if (!self || self->dead) return;

    float cam_x = self->x, cam_y = self->y, cam_z = self->z;

    /* Mark all non-self live entities as candidates. */
    for (int i = 0; i < count; i++) {
        Entity *e = &entities[i];
        if (e == self || e->dead || !e->actor_address) continue;
        if (!e->position_plaintext && (e->x == 0 && e->y == 0 && e->z == 0)) continue;
        e->visibility_known = 1; /* placeholder: ev_execute_traces will fill */
    }

    ev_execute_traces(count, cam_x, cam_y, cam_z, world, pawn);

    /* Aim integration: compute and apply on scanner thread. */
    unsigned aim_config = atomic_load(&ea_config);
    if (self->dead || !local_player_controlled) aim_config = 0;
    if (aim_config & 1) {
        ea_bind_controller(pc);
        ea_frame_direct(count, cam_x, cam_y, cam_z, self->yaw, self->pitch,
                        aim_config, world, pc, pawn);
    } else {
        ea_release();
    }
}
#endif
