#ifndef INTERNAL_ENGINE_TRIGGER_H
#define INTERNAL_ENGINE_TRIGGER_H
#include "engine_fire.h"
/* Scanner-owned crosshair decision, executed by the engine on GameThread. */
static _Atomic unsigned et_status, et_queries;
static _Atomic uint64_t et_hit_actor;

static void et_publish(int enabled, int fire, uint64_t pawn, uint64_t controller, uint64_t target) {
    ef_publish(enabled, fire, pawn, controller, target);
    atomic_store(&et_status, atomic_load(&ef_status));
}
static void et_release(void) {
    atomic_store(&et_hit_actor, 0);
    et_publish(0, 0, 0, 0, 0);
}
static int et_enemy(const Entity *e, uint64_t pawn) {
    return e->actor_address && e->actor_address != pawn &&
        strcmp(e->id, self_entity_id) && !e->dead &&
        isfinite(e->health) && e->health > 0.01f &&
        e->visibility_known && e->visible_from_camera &&
        (e->kind[0] == 'a' || (e->kind[0] == 'p' && e->team != 1)) &&
        ea_alive_entity(e);
}
static void et_frame(int count, float x, float y, float z, float yaw, float pitch,
                     unsigned config, uint64_t world, uint64_t controller, uint64_t pawn) {
    if (!(config & (1u << 24)) || !ev_pe_fn ||
        !ev_same(&ev_function) || !ev_same(&ev_receiver) ||
        !isfinite(x) || !isfinite(y) || !isfinite(z) ||
        !isfinite(yaw) || !isfinite(pitch)) { et_release(); return; }
    EVTraceParams params = {0};
    params.world = world;
    params.start[0] = x * 100; params.start[1] = y * 100; params.start[2] = z * 100;
    const float radians = 0.017453292519943295f, range = 200000.0f;
    float cy = cosf(yaw * radians), sy = sinf(yaw * radians);
    float cp = cosf(pitch * radians), sp = sinf(pitch * radians);
    params.end[0] = params.start[0] + range * cp * cy;
    params.end[1] = params.start[1] + range * cp * sy;
    params.end[2] = params.start[2] + range * sp;
    params.channel = 3;
    params.complex = 1;
    params.ignore = (EVArray){(uint64_t)(uintptr_t)&pawn, 1, 1};
    params.result = 0xff;
    ev_pe_fn((void *)(uintptr_t)ev_receiver.address,
             (void *)(uintptr_t)ev_function.address, &params);
    atomic_fetch_add(&et_queries, 1);
    uint64_t hit = 0, item = 0;
    int32_t index = -1, serial = 0, live_serial = 0;
    /* SDK FHitResult.Actor: +0x68. This build's FUObjectItem weak serial
     * is +0x14 (also verified by engine_weapon), not the +0x10 flags. */
    memcpy(&index, params.hit + 0x68, 4);
    memcpy(&serial, params.hit + 0x6c, 4);
    if (params.result == 1 && (params.hit[0] & 1) && serial > 0 &&
        ev_item(index, &item) && ev_read(item + 0x14, &live_serial, 4) &&
        serial == live_serial) hit = ev_ptr(item);
    int fire = 0;
    for (int i = 0; hit && i < count && i < EV_MAX; ++i)
        if (entities[i].actor_address == hit && et_enemy(&entities[i], pawn)) { fire = 1; break; }
    atomic_store(&et_hit_actor, hit);
    et_publish(1, fire, pawn, controller, hit);
}
#endif
