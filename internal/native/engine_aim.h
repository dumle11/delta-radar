#ifndef INTERNAL_ENGINE_AIM_H
#define INTERNAL_ENGINE_AIM_H
/* Direct memory-write aim. No ProcessEvent calls for rotation — writes
 * ControlRotation directly on the controller. Runs on the scanner thread.
 * No vtable hooks, no game-thread interception. */
static _Atomic unsigned ea_config, ea_status, ea_corrections;
static _Atomic uint64_t ea_locked_actor;
static _Atomic int ea_ready;
static _Atomic int ea_autoshoot_active;
static _Atomic int ea_screen_w, ea_screen_h;
static EVObject ea_controller_obj, ea_target, ea_world_obj, ea_pawn_obj;

/* SetIgnoreLookInput offset: APlayerController::bIgnoreLookInput is a
 * counter-style flag. We write ControlRotation directly each frame instead
 * of suppressing input — this avoids calling any engine functions. */

/* ControlRotation: Pitch at +0x3D8, Yaw at +0x3DC (from existing offsets). */
#define EA_OFF_CONTROL_PITCH 0x3D8
#define EA_OFF_CONTROL_YAW   0x3DC

static int ea_alive(uint64_t actor) {
    uint8_t dead = 1;
    return is_any_ptr(actor) &&
        ev_read(actor + OFF_CHARACTER_LAST_IS_DEAD, &dead, 1) && !(dead & 1);
}

static void ea_bind_controller(uint64_t controller) {
    if (atomic_load(&ea_ready)) return;
    if (!is_any_ptr(controller)) return;
    /* Walk the class hierarchy to verify this is actually a controller. */
    char want[16];
    static const unsigned char ec[] = {0x16,0x3a,0x3b,0x21,0x27,0x3a,0x39,0x39,0x30,0x27};
    ev_decode(want, ec, 10);
    for (uint64_t cls = read_ptr(controller + 8), n = 0; cls && n < 32; n++, cls = read_ptr(cls + 0x40)) {
        char name[80];
        resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, sizeof(name));
        if (!strcmp(name, want)) { atomic_store(&ea_ready, 1); return; }
    }
}

static void ea_release(void) {
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

static void ea_frame_direct(int count, float cam_x, float cam_y, float cam_z,
                            float cam_yaw, float cam_pitch, unsigned aim_config,
                            uint64_t world, uint64_t controller, uint64_t pawn) {
    if (!(aim_config & 1) || !atomic_load(&ea_ready)) {
        ea_release();
        if ((aim_config & 1) && !atomic_load(&ea_ready)) atomic_store(&ea_status, 3);
        return;
    }

    float camera_cm[3] = {cam_x * 100.0f, cam_y * 100.0f, cam_z * 100.0f};

    /* Build aim choices from current entity state. */
    AimChoice choices[EV_MAX];
    int choice_count = 0;
    int choice_entity[EV_MAX];
    unsigned mask = (aim_config >> 1) & 7;
    int bone = (aim_config >> 16) & 3;
    float height = bone == 0 ? 0.8f : bone == 1 ? 0.45f : 0.2f;

    for (int i = 0; i < count && choice_count < EV_MAX; i++) {
        Entity *e = &entities[i];
        if (!strcmp(e->id, self_entity_id) || e->dead) continue;
        if (!e->actor_address) continue;

        int allowed = e->kind[0] == 'a' ? (mask & 4) != 0 :
            e->kind[0] == 'p' && ((e->team != 1 && (mask & 1) != 0) ||
                                   (e->team == 1 && (mask & 2) != 0));
        int eligible = allowed && e->health_known && isfinite(e->health) && e->health > 0.01f
            && e->visibility_known && e->visible_from_camera;

        float aim_point[3] = {
            e->x * 100.0f,
            e->y * 100.0f,
            (e->z + e->half_height * height) * 100.0f
        };

        /* Check if aim point is clear via engine trace. */
        int aim_clear = 0;
        if (eligible && ev_pe_fn && ev_same(&ev_function) && ev_same(&ev_receiver)) {
            uint64_t ignored[2] = {pawn, e->actor_address};
            EVTraceParams params = {0};
            params.world = world;
            params.start[0] = camera_cm[0]; params.start[1] = camera_cm[1]; params.start[2] = camera_cm[2];
            params.end[0] = aim_point[0]; params.end[1] = aim_point[1]; params.end[2] = aim_point[2];
            params.channel = 3; params.complex = 1;
            params.ignore = (EVArray){(uint64_t)(uintptr_t)ignored, 2, 2};
            params.result = 0xff;
            ev_pe_fn((void *)(uintptr_t)ev_receiver.address,
                     (void *)(uintptr_t)ev_function.address, &params);
            atomic_fetch_add(&ev_traces, 1);
            if (params.result == 0) {
                aim_clear = 1;
            } else if (params.result == 1) {
                /* Try exposed body sample instead of blocked aim point. */
                const int preference[5] = {0, 1, 3, 4, 2};
                for (int k = 0; k < 5; k++) {
                    if (!(e->visibility_ray_mask & (1u << preference[k]))) {
                        float sample[3];
                        visibility_sample_point(cam_x, cam_y, e->x, e->y, e->z, e->half_height, preference[k], sample);
                        aim_point[0] = sample[0] * 100.0f;
                        aim_point[1] = sample[1] * 100.0f;
                        aim_point[2] = sample[2] * 100.0f;
                        aim_clear = 1;
                        break;
                    }
                }
            }
        }

        choices[choice_count] = (AimChoice){
            e->actor_address,
            aim_screen_distance(camera_cm, cam_yaw, cam_pitch, aim_point),
            eligible && aim_clear && ea_alive(e->actor_address)
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

    int chosen = aim_select(choices, choice_count, previous, (aim_config >> 8) & 255);
    if (chosen < 0) { ea_release(); return; }

    Entity *target_entity = &entities[choice_entity[chosen]];
    if (target_entity->actor_address != previous) {
        char ad[128];
        snprintf(ad,sizeof(ad),"aim lock: kind=%s team=%d name=%.30s mask=%u act=%lx",
            target_entity->kind, target_entity->team, target_entity->name, mask,
            (unsigned long)target_entity->actor_address);
        _diag(ad);
    }
    float aim_point[3] = {
        target_entity->x * 100.0f,
        target_entity->y * 100.0f,
        (target_entity->z + target_entity->half_height * height) * 100.0f
    };
    float rotation[3];
    if (!aim_rotation(camera_cm, aim_point, rotation)) { ea_release(); return; }

    ev_object(controller, &ea_controller_obj);
    ev_object(world, &ea_world_obj);
    ev_object(pawn, &ea_pawn_obj);
    ev_object(target_entity->actor_address, &ea_target);
    atomic_store(&ea_locked_actor, ea_target.address);
    atomic_store(&ea_status, 2);

    /* Write rotation directly to controller memory. */
    ea_write_rotation(controller, rotation[0], rotation[1]);

}
#endif
