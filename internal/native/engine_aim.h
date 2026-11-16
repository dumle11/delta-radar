#ifndef INTERNAL_ENGINE_AIM_H
#define INTERNAL_ENGINE_AIM_H
#include "aim_target.h"
/* Direct memory-write aim + direct ProcessEvent autoshoot.
 * No vtable hooks, no mprotect, no game-thread interception. */
static _Atomic unsigned ea_config, ea_status, ea_corrections;
static _Atomic uint64_t ea_locked_actor;
static _Atomic int ea_ready;
static _Atomic int ea_autoshoot_active;
static _Atomic int ea_screen_w, ea_screen_h;
static EVObject ea_controller_obj, ea_target, ea_world_obj, ea_pawn_obj;

static EVObject ea_fire_start_fn, ea_fire_stop_fn;
static uint64_t ea_fire_bound_class;
static _Atomic unsigned ea_fire_starts, ea_fire_stops;
static int64_t ea_fire_last_send;

static EVObject ea_zoom_aim_fn, ea_zoom_unaim_fn, ea_zoom_progress_fn, ea_zoom_is_on_fn;
static EVObject ea_zoom_input_obj, ea_zoom_proxy_obj;
static uint64_t ea_zoom_bound_weapon;
static int ea_zoom_owned;
static _Atomic int ea_zoom_progress_val;
static _Atomic unsigned ea_zooms, ea_unzooms;
static uint64_t ew_find_component_in(uint64_t weapon, const char *name, uint32_t size, uint32_t list_offset);

/* SetIgnoreLookInput offset: APlayerController::bIgnoreLookInput is a
 * counter-style flag. We write ControlRotation directly each frame instead
 * of suppressing input — this avoids calling any engine functions. */

/* ControlRotation: Pitch at +0x3D8, Yaw at +0x3DC (from existing offsets). */
#define EA_OFF_CONTROL_PITCH 0x3D8
#define EA_OFF_CONTROL_YAW   0x3DC

static int ea_alive_raw(uint64_t actor) {
    uint8_t dead = 1;
    return ev_native_frame_current() && ev_actor_present(actor) && is_any_ptr(actor) &&
        ev_read(actor + OFF_CHARACTER_LAST_IS_DEAD, &dead, 1) && !(dead & 1);
}
static int ea_alive_entity(const Entity *e) {
    if (e->health_known && e->health > 0.5f) return 1;
    return ea_alive_raw(e->actor_address);
}

static void ea_bind_controller(uint64_t controller) {
    if (atomic_load(&ea_ready)) return;
    if (!is_any_ptr(controller)) return;
    char want[16];
    static const unsigned char ec[] = {0x16,0x3a,0x3b,0x21,0x27,0x3a,0x39,0x39,0x30,0x27};
    ev_decode(want, ec, 10);
    for (uint64_t cls = read_ptr(controller + 8), n = 0; cls && n < 32; n++, cls = read_ptr(cls + 0x40)) {
        char name[80];
        resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, sizeof(name));
        if (!strcmp(name, want)) { atomic_store(&ea_ready, 1); return; }
    }
}

static int ea_bind_fire(uint64_t pawn) {
    uint64_t cls = ev_ptr(pawn + 8);
    if (cls == ea_fire_bound_class && ea_fire_start_fn.address && ea_fire_stop_fn.address)
        return ev_same(&ea_fire_start_fn) && ev_same(&ea_fire_stop_fn);
    memset(&ea_fire_start_fn, 0, sizeof(ea_fire_start_fn));
    memset(&ea_fire_stop_fn, 0, sizeof(ea_fire_stop_fn));
    ea_fire_bound_class = 0;
    static const unsigned char _ea_gcb[] = {0x12,0x05,0x16,0x3d,0x34,0x27,0x34,0x36,0x21,0x30,0x27,0x17,0x34,0x26,0x30};
    static const unsigned char _ea_sf[] = {0x06,0x21,0x34,0x27,0x21,0x13,0x3c,0x27,0x30};
    static const unsigned char _ea_xf[] = {0x06,0x21,0x3a,0x25,0x13,0x3c,0x27,0x30};
    char want_gcb[16], want_sf[10], want_xf[10];
    ev_decode(want_gcb, _ea_gcb, 15);
    ev_decode(want_sf, _ea_sf, 9);
    ev_decode(want_xf, _ea_xf, 8);
    for (uint64_t cur = cls, n = 0; cur && n < 32; n++, cur = ev_ptr(cur + 0x40)) {
        char name[80]; resolve_fname(read_i32(cur + OFF_UOBJECT_NAME), name, sizeof(name));
        if (strcmp(name, want_gcb)) continue;
        for (uint64_t field = ev_ptr(cur + 0x50), i = 0; field && i < 512; i++, field = ev_ptr(field + 0x28)) {
            resolve_fname(read_i32(field + OFF_UOBJECT_NAME), name, sizeof(name));
            int is_start = !strcmp(name, want_sf), is_stop = !strcmp(name, want_xf);
            if (!is_start && !is_stop) continue;
            if (ev_ptr(field + 0x10) != cur) continue;
            uint16_t bytes = 0; uint32_t flags = 0;
            if (!ev_read(field + 0xb2, &bytes, 2) || bytes != 4) continue;
            if (!ev_read(field + 0xb8, &flags, 4) || !(flags & 0x400)) continue;
            ev_object(field, is_start ? &ea_fire_start_fn : &ea_fire_stop_fn);
        }
        break;
    }
    ea_fire_bound_class = cls;
    return ea_fire_start_fn.address && ea_fire_stop_fn.address;
}

static void ea_fire_stop(uint64_t pawn) {
    if (atomic_load(&ea_autoshoot_active) && ev_pe_fn &&
        ea_fire_stop_fn.address && ev_same(&ea_fire_stop_fn) &&
        ea_alive_raw(pawn)) {
        int32_t fire_index = 0;
        ev_pe_fn((void *)(uintptr_t)pawn,
                 (void *)(uintptr_t)ea_fire_stop_fn.address, &fire_index);
        atomic_fetch_add(&ea_fire_stops, 1);
    }
    atomic_store(&ea_autoshoot_active, 0);
}

static int ea_find_method(uint64_t object, const unsigned char *owner_enc, int owner_len,
                          const unsigned char *wanted_enc, int wanted_len,
                          uint16_t expected_size, uint64_t exec_rva, EVObject *out) {
    memset(out, 0, sizeof(*out));
    char owner[40], wanted[40];
    ev_decode(owner, owner_enc, owner_len);
    ev_decode(wanted, wanted_enc, wanted_len);
    for (uint64_t cls = ev_ptr(object + 8), n = 0; cls && n < 32; n++, cls = ev_ptr(cls + 0x40)) {
        char name[80]; resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, sizeof(name));
        if (strcmp(name, owner)) continue;
        for (uint64_t field = ev_ptr(cls + 0x50), i = 0; field && i < 512; i++, field = ev_ptr(field + 0x28)) {
            resolve_fname(read_i32(field + OFF_UOBJECT_NAME), name, sizeof(name));
            if (strcmp(name, wanted) || ev_ptr(field + 0x10) != cls) continue;
            uint16_t size = 0; uint32_t flags = 0;
            return ev_read(field + 0xb2, &size, 2) && size == expected_size &&
                ev_read(field + 0xb8, &flags, 4) && (flags & 0x400) &&
                ev_ptr(field + 0xd8) == ue4_base + exec_rva && ev_object(field, out);
        }
        break;
    }
    return 0;
}

static int ea_zoom_bind(uint64_t controller, uint64_t weapon) {
    if (ea_zoom_bound_weapon == weapon && ea_zoom_aim_fn.address &&
        ev_same(&ea_zoom_aim_fn) && ev_same(&ea_zoom_unaim_fn) &&
        ev_same(&ea_zoom_progress_fn) && ev_same(&ea_zoom_is_on_fn) &&
        ev_same(&ea_zoom_input_obj) && ev_same(&ea_zoom_proxy_obj)) return 1;
    memset(&ea_zoom_aim_fn, 0, sizeof(ea_zoom_aim_fn));
    memset(&ea_zoom_unaim_fn, 0, sizeof(ea_zoom_unaim_fn));
    memset(&ea_zoom_progress_fn, 0, sizeof(ea_zoom_progress_fn));
    memset(&ea_zoom_is_on_fn, 0, sizeof(ea_zoom_is_on_fn));
    memset(&ea_zoom_input_obj, 0, sizeof(ea_zoom_input_obj));
    memset(&ea_zoom_proxy_obj, 0, sizeof(ea_zoom_proxy_obj));
    ea_zoom_bound_weapon = 0;
    static const unsigned char _gpim[] = {0x12,0x05,0x1c,0x3b,0x25,0x20,0x21,0x18,0x34,0x3b,0x34,0x32,0x30,0x27};
    static const unsigned char _oika[] = {0x1a,0x3b,0x1c,0x3b,0x25,0x20,0x21,0x1e,0x30,0x2c,0x14,0x3c,0x38};
    static const unsigned char _oiku[] = {0x1a,0x3b,0x1c,0x3b,0x25,0x20,0x21,0x1e,0x30,0x2c,0x00,0x3b,0x14,0x3c,0x38};
    static const unsigned char _wdcp[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x11,0x34,0x21,0x34,0x16,0x3a,0x38,0x25,0x3a,0x3b,0x30,0x3b,0x21,0x05,0x27,0x3a,0x2d,0x2c};
    static const unsigned char _gzp[] = {0x12,0x30,0x21,0x0f,0x3a,0x3a,0x38,0x05,0x27,0x3a,0x32,0x27,0x30,0x26,0x26};
    static const unsigned char _gpwdcp[] = {0x12,0x05,0x02,0x30,0x34,0x25,0x3a,0x3b,0x11,0x34,0x21,0x34,0x16,0x3a,0x38,0x25,0x3a,0x3b,0x30,0x3b,0x21,0x05,0x27,0x3a,0x2d,0x2c};
    static const unsigned char _iz[] = {0x1c,0x26,0x0f,0x3a,0x3a,0x38,0x3c,0x3b,0x32};
    char s_wdcp[25]; ev_decode(s_wdcp, _wdcp, 24);
    uint64_t input = ev_ptr(controller + 0x960);
    uint64_t proxy = ew_find_component_in(weapon, s_wdcp, 0x878, 0xac8);
    if (!input || !proxy) return 0;
    if (!ev_object(input, &ea_zoom_input_obj) || !ev_object(proxy, &ea_zoom_proxy_obj)) return 0;
    if (!ea_find_method(input, _gpim, 14, _oika, 13, 0, 0xc5e816c, &ea_zoom_aim_fn) ||
        !ea_find_method(input, _gpim, 14, _oiku, 15, 0, 0xc5e8604, &ea_zoom_unaim_fn) ||
        !ea_find_method(proxy, _wdcp, 24, _gzp, 15, 4, 0xc6f424c, &ea_zoom_progress_fn) ||
        !ea_find_method(proxy, _gpwdcp, 26, _iz, 9, 1, 0x9e9bd34, &ea_zoom_is_on_fn)) return 0;
    ea_zoom_bound_weapon = weapon;
    return 1;
}

static void ea_zoom_release(void) {
    if (ea_zoom_owned && ev_native_frame_current() && ev_pe_fn && ea_zoom_unaim_fn.address &&
        ev_same(&ea_zoom_input_obj) && ev_same(&ea_zoom_unaim_fn)) {
        ev_pe_fn((void *)(uintptr_t)ea_zoom_input_obj.address,
                 (void *)(uintptr_t)ea_zoom_unaim_fn.address, NULL);
        atomic_fetch_add(&ea_unzooms, 1);
    }
    ea_zoom_owned = 0;
    atomic_store(&ea_zoom_progress_val, 0);
}

static int ea_zoom_gate(uint64_t controller, uint64_t weapon) {
    if (!ea_zoom_bind(controller, weapon)) return 0;
    uint8_t on = 0xff;
    float progress = NAN;
    ev_pe_fn((void *)(uintptr_t)ea_zoom_proxy_obj.address,
             (void *)(uintptr_t)ea_zoom_is_on_fn.address, &on);
    ev_pe_fn((void *)(uintptr_t)ea_zoom_proxy_obj.address,
             (void *)(uintptr_t)ea_zoom_progress_fn.address, &progress);
    if (on > 1 || !isfinite(progress) || progress < 0 || progress > 1.01f) return 0;
    atomic_store(&ea_zoom_progress_val, (int)(progress * 1000));
    if (!on && !ea_zoom_owned) {
        ev_pe_fn((void *)(uintptr_t)ea_zoom_input_obj.address,
                 (void *)(uintptr_t)ea_zoom_aim_fn.address, NULL);
        ea_zoom_owned = 1;
        atomic_fetch_add(&ea_zooms, 1);
        return 0;
    }
    return on && progress >= 0.99f;
}

static void ea_release(void) {
    ea_fire_stop(ea_pawn_obj.address);
    ea_zoom_release();
    memset(&ea_controller_obj, 0, sizeof(ea_controller_obj));
    memset(&ea_target, 0, sizeof(ea_target));
    atomic_store(&ea_locked_actor, 0);
    atomic_store(&ea_status, (atomic_load(&ea_config) & 1) ? 1 : 0);
}

/* Write ControlRotation directly to the controller's memory. */
static void ea_write_rotation(uint64_t controller, float pitch, float yaw) {
    if (!is_any_ptr(controller)) return;
    /* Direct memory write — no function calls into the engine. */
    volatile float *pitch_addr = (volatile float *)(uintptr_t)(controller + EA_OFF_CONTROL_PITCH);
    volatile float *yaw_addr = (volatile float *)(uintptr_t)(controller + EA_OFF_CONTROL_YAW);
    *pitch_addr = pitch;
    *yaw_addr = yaw;
    atomic_fetch_add(&ea_corrections, 1);
}

typedef struct {uint64_t world,pawn,target;float start[3];} EAAimTrace;
static int ea_trace_aim_point(void *context,const float *point) {
    if (!ev_native_frame_current()) return -1;
    EAAimTrace *trace=context;uint64_t ignored[]={trace->pawn,trace->target};
    EVTraceParams params={0};params.world=trace->world;
    memcpy(params.start,trace->start,sizeof(params.start));memcpy(params.end,point,sizeof(params.end));
    params.channel=3;params.complex=1;params.ignore=(EVArray){(uint64_t)(uintptr_t)ignored,2,2};params.result=0xff;
    ev_pe_fn((void *)(uintptr_t)ev_receiver.address,(void *)(uintptr_t)ev_function.address,&params);
    atomic_fetch_add(&ev_traces,1);return params.result;
}
static void ea_frame_direct(int count, float cam_x, float cam_y, float cam_z,
                            float cam_yaw, float cam_pitch, unsigned aim_config,
                            uint64_t world, uint64_t controller, uint64_t pawn) {
    int aim_locked = (aim_config >> 26) & 1;
    int aim_360 = (aim_config >> 27) & 1;
    int aim_priority = (aim_config >> 28) & 3;
    if (!(aim_config & 1) || !atomic_load(&ea_ready) || !aim_locked) {
        ea_release();
        if ((aim_config & 1) && !aim_locked) atomic_store(&ea_status, 1);
        else if ((aim_config & 1) && !atomic_load(&ea_ready)) atomic_store(&ea_status, 3);
        return;
    }

    float camera_cm[3] = {cam_x * 100.0f, cam_y * 100.0f, cam_z * 100.0f};

    /* Build aim choices from current entity state. */
    AimChoice choices[EV_MAX];
    int choice_count = 0;
    int choice_entity[EV_MAX];
    float aim_points[EV_MAX][3];
    unsigned mask = (aim_config >> 1) & 7;
    int bone = (aim_config >> 16) & 3;

    for (int i = 0; i < count && choice_count < EV_MAX; i++) {
        Entity *e = &entities[i];
        if (!strcmp(e->id, self_entity_id) || e->dead) continue;
        if (!e->actor_address) continue;

        int allowed = e->kind[0] == 'a' ? (mask & 4) != 0 :
            e->kind[0] == 'p' && ((e->team != 1 && (mask & 1) != 0) ||
                                   (e->team == 1 && (mask & 2) != 0));
        int eligible = allowed && e->health_known && isfinite(e->health) && e->health > 0.01f;

        float aim_point[3];
        if(!aim_entity_point(e,bone,aim_point)) continue;

        EAAimTrace trace={.world=world,.pawn=pawn,.target=e->actor_address};
        memcpy(trace.start,camera_cm,sizeof(trace.start));
        int aim_clear=eligible && ev_pe_fn && ev_same(&ev_function) && ev_same(&ev_receiver) &&
            aim_visible_entity_point(e,bone,aim_point,ea_trace_aim_point,&trace);

        aim_points[choice_count][0] = aim_point[0];
        aim_points[choice_count][1] = aim_point[1];
        aim_points[choice_count][2] = aim_point[2];
        float range_dist = aim_360 ? aim_3d_distance_sq(camera_cm, aim_point)
                                    : aim_screen_distance(camera_cm, cam_yaw, cam_pitch, aim_point);
        float score;
        switch (aim_priority) {
        default:
        case 0: score = range_dist; break;
        case 1: score = aim_3d_distance_sq(camera_cm, aim_point); break;
        case 2: score = (e->health_known && isfinite(e->health)) ? e->health : 1e9f; break;
        case 3: {
            float dx = cam_x - e->x, dy = cam_y - e->y;
            float to_us_yaw = atan2f(dy, dx) * 57.29577951308232f;
            float delta = e->yaw - to_us_yaw;
            while (delta > 180.0f) delta -= 360.0f;
            while (delta < -180.0f) delta += 360.0f;
            int facing = fabsf(delta) < 60.0f;
            float dist3d = aim_3d_distance_sq(camera_cm, aim_point);
            score = facing ? dist3d : (1e12f + dist3d);
            break;
        }
        }
        choices[choice_count] = (AimChoice){
            e->actor_address, score, range_dist,
            eligible && aim_clear && ea_alive_entity(e)
        };
        choice_entity[choice_count] = i;
        choice_count++;
    }

    uint64_t previous = ea_target.address && ev_same(&ea_target) ? ea_target.address : 0;
    if (ea_controller_obj.address != controller || ea_world_obj.address != world ||
        ea_pawn_obj.address != pawn) {
        ea_release();
        previous = 0;
    }

    int chosen = aim_select(choices, choice_count, previous, (aim_config >> 8) & 255, aim_360);
    if (chosen < 0) { ea_release(); return; }

    Entity *target_entity = &entities[choice_entity[chosen]];
    if (target_entity->actor_address != previous) {
        char ad[128];
        snprintf(ad,sizeof(ad),"aim lock: kind=%s team=%d name=%.20s mask=%u act=%lx bone=%d joint=%d",
            target_entity->kind, target_entity->team, target_entity->name, mask,
            (unsigned long)target_entity->actor_address,bone,target_entity->aim_bone_index[bone]);
        _diag(ad);
    }
    float rotation[3];
    if (!aim_rotation(camera_cm, aim_points[chosen], rotation)) { ea_release(); return; }

    ev_object(controller, &ea_controller_obj);
    ev_object(world, &ea_world_obj);
    ev_object(pawn, &ea_pawn_obj);
    ev_object(target_entity->actor_address, &ea_target);
    atomic_store(&ea_locked_actor, ea_target.address);
    atomic_store(&ea_status, 2);

    /* Write rotation directly to controller memory. */
    ea_write_rotation(controller, rotation[0], rotation[1]);

    int autoshoot = (aim_config >> 24) & 1;
    int auto_zoom = (aim_config >> 25) & 1;
    int was_shooting = atomic_load(&ea_autoshoot_active);
    if (autoshoot && choices[chosen].eligible && ev_pe_fn && ea_bind_fire(pawn)) {
        int zoom_ready = 1;
        if (auto_zoom) {
            uint64_t weapon = ev_ptr(ev_ptr(pawn + 0x3b18) + 0x218);
            zoom_ready = weapon && ea_zoom_gate(controller, weapon);
        }
        if (zoom_ready) {
            struct timespec _now; clock_gettime(CLOCK_MONOTONIC, &_now);
            int64_t now_ms = (int64_t)_now.tv_sec * 1000 + _now.tv_nsec / 1000000;
            if (!was_shooting || (now_ms - ea_fire_last_send > 150)) {
                int32_t fire_index = 0;
                ev_pe_fn((void *)(uintptr_t)pawn,
                         (void *)(uintptr_t)ea_fire_start_fn.address, &fire_index);
                ea_fire_last_send = now_ms;
                if (!was_shooting) {
                    atomic_store(&ea_autoshoot_active, 1);
                    atomic_fetch_add(&ea_fire_starts, 1);
                }
            }
        } else if (was_shooting) {
            ea_fire_stop(pawn);
        }
    } else {
        if (was_shooting) ea_fire_stop(pawn);
        if (ea_zoom_owned) ea_zoom_release();
    }
}
#endif
