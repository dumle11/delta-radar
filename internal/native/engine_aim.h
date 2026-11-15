#ifndef INTERNAL_ENGINE_AIM_H
#define INTERNAL_ENGINE_AIM_H
/* Included by engine_visibility.h. All lock/input ownership lives exclusively
 * on the UE GameThread; the worker publishes only settings and snapshots. */
static _Atomic unsigned ea_config, ea_status, ea_corrections;
static _Atomic uint64_t ea_locked_actor;
static _Atomic int ea_ready;
static EVObject ea_rotate_function, ea_ignore_function;
static EVObject ea_controller, ea_target, ea_world, ea_pawn;
static float ea_rotation[3];
static int64_t ea_sample_ms;
static int ea_owns_ignore;

static int ea_function(uint64_t cls, const char *wanted, unsigned size, EVObject *out) {
    for (uint64_t field=read_ptr(cls+0x50), n=0; field && n<512; n++,field=read_ptr(field+0x28)) {
        char name[80]; resolve_fname(read_i32(field+0x1c),name,sizeof(name));
        if (strcmp(name,wanted) || read_ptr(field+0x10)!=cls) continue;
        uint16_t extent=0; uint32_t flags=0;
        if (!ev_read(field+0xb2,&extent,2) || extent!=size ||
            !ev_read(field+0xb8,&flags,4) || !(flags&0x400)) return 0;
        return ev_object(field,out);
    }
    return 0;
}
static void ea_bind_controller(uint64_t controller) {
    if (atomic_load(&ea_ready)) return;
    for (uint64_t cls=read_ptr(controller+8), n=0; cls && n<32; n++,cls=read_ptr(cls+0x40)) {
        char name[80]; resolve_fname(read_i32(cls+0x1c),name,sizeof(name));
        if (strcmp(name,"Controller")) continue;
        if (ea_function(cls,"SetControlRotation",12,&ea_rotate_function) &&
            ea_function(cls,"SetIgnoreLookInput",1,&ea_ignore_function)) {
            atomic_store(&ea_ready,1);
        }
        return;
    }
}
static int ea_alive(const EVObject *actor) {
    uint8_t dead=1;
    return ev_same(actor) && ev_read(actor->address+OFF_CHARACTER_LAST_IS_DEAD,&dead,1) && !(dead&1);
}
static void ea_release(void) {
    /* SetIgnoreLookInput is a counter. Balance exactly our one increment;
     * ResetIgnoreLookInput would also remove locks owned by the host. */
    if (ea_owns_ignore && ev_same(&ea_controller) && ev_same(&ea_ignore_function)) {
        uint8_t ignored=0;
        ev_original((void *)(uintptr_t)ea_controller.address,
            (void *)(uintptr_t)ea_ignore_function.address,&ignored);
    }
    ea_owns_ignore=0;
    memset(&ea_controller,0,sizeof(ea_controller));
    memset(&ea_target,0,sizeof(ea_target));
    atomic_store(&ea_locked_actor,0);
    atomic_store(&ea_status,(atomic_load(&ea_config)&1) ? 1 : 0);
}
static void ea_frame(EVFrame *frame) {
    if (!(frame->aim_config&1) || !(atomic_load(&ea_config)&1) || !atomic_load(&ea_ready) ||
        !ev_same(&ea_rotate_function) || !ev_same(&ea_ignore_function)) {
        ea_release();
        if ((frame->aim_config&1) && !atomic_load(&ea_ready)) atomic_store(&ea_status,3);
        return;
    }
    AimChoice choices[EV_MAX];
    for (int i=0;i<frame->count;i++) {
        EVJob *job=&frame->jobs[i];
        choices[i]=(AimChoice){job->actor.address,
            aim_screen_distance(frame->camera,frame->yaw,frame->pitch,job->aim_point),
            job->aim_eligible && job->aim_clear && job->visible==1 && ea_alive(&job->actor)};
    }
    uint64_t previous=ev_same(&ea_target) ? ea_target.address : 0;
    if (!ev_same(&ea_controller) || !ev_same(&ea_world) || !ev_same(&ea_pawn) ||
        ea_controller.address!=frame->controller.address || ea_world.address!=frame->world.address ||
        ea_pawn.address!=frame->pawn.address) { ea_release(); previous=0; }
    int chosen=aim_select(choices,frame->count,previous,(frame->aim_config>>8)&255);
    if (chosen<0) { ea_release(); return; }
    EVJob *job=&frame->jobs[chosen];
    float rotation[3];
    if (!aim_rotation(frame->camera,job->aim_point,rotation)) { ea_release(); return; }
    ea_controller=frame->controller; ea_world=frame->world; ea_pawn=frame->pawn;
    ea_target=job->actor; ea_sample_ms=frame->at_ms;
    memcpy(ea_rotation,rotation,sizeof(rotation));
    atomic_store(&ea_locked_actor,ea_target.address);
    atomic_store(&ea_status,2);
}
static void ea_tick(void) {
    int64_t age=ev_now()-ea_sample_ms;
    if (!(atomic_load(&ea_config)&1) || !atomic_load(&ev_ready) || !atomic_load(&lib_running_flag) ||
        !ea_target.address || age<0 || age>EV_MAX_AGE_MS || !ea_alive(&ea_target) ||
        !ea_alive(&ea_pawn) || !ev_same(&ea_world) || !ev_same(&ea_controller) ||
        !ev_same(&ea_rotate_function) || !ev_same(&ea_ignore_function) ||
        ev_ptr(ea_controller.address+0x3f0)!=ea_pawn.address) {
        if (ea_target.address || ea_owns_ignore) ea_release();
        return;
    }
    if (!ea_owns_ignore) {
        uint8_t ignored=1;
        ev_original((void *)(uintptr_t)ea_controller.address,
            (void *)(uintptr_t)ea_ignore_function.address,&ignored);
        ea_owns_ignore=1;
    }
    ev_original((void *)(uintptr_t)ea_controller.address,
        (void *)(uintptr_t)ea_rotate_function.address,ea_rotation);
    atomic_fetch_add(&ea_corrections,1);
}
#endif
