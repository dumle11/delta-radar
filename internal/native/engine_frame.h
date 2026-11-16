#ifndef INTERNAL_ENGINE_FRAME_H
#define INTERNAL_ENGINE_FRAME_H
/* Reconnect/travel can leave registered objects from the previous world alive.
 * Registration alone does not establish that it is safe to call into them. */
static struct { EVObject world, controller, pawn, camera; uint64_t slot; int valid; } ev_frame;
static int ev_actor_present(uint64_t actor) {
    uint16_t state;
    /* Exact IsActorBeingDestroyed implementation reads bit 9 of +0x94. */
    return ev_read(actor+0x94,&state,2) && !(state & (1u<<9));
}
static int ev_native_frame_current(void) {
    return ev_frame.valid && ev_ptr(ev_frame.slot)==ev_frame.world.address &&
        ev_ptr(ev_frame.controller.address+0x3f0)==ev_frame.pawn.address &&
        ev_actor_present(ev_frame.controller.address) && ev_actor_present(ev_frame.pawn.address) &&
        ev_actor_present(ev_frame.camera.address);
}
/* A registered Engine/viewport outlives its temporary null World/level during
 * travel. Keep following it instead of restarting whole-address-space scans. */
static int ev_viewport_registered(void) {
    EVObject engine,viewport;
    return ev_object(cached_engine,&engine) &&
        ev_object(ev_ptr(cached_engine+OFF_UENGINE_GAME_VIEWPORT),&viewport);
}
static int ev_frame_begin(void) {
    ev_frame.valid=0;
    uint64_t viewport=ev_ptr(cached_engine+OFF_UENGINE_GAME_VIEWPORT);
    ev_frame.slot=viewport+OFF_VIEWPORT_WORLD;
    uint64_t world=ev_ptr(ev_frame.slot),gi=ev_ptr(world+0x190),local=ev_ptr(ev_ptr(gi+0x38));
    uint64_t pc=ev_ptr(local+0x30),pawn=ev_ptr(pc+0x3f0),camera=ev_ptr(pc+0x408);
    if(!camera_frame_valid || !local_player_controlled || world!=diagnostic_world ||
        camera!=resolved_camera_owner || ev_ptr(camera+0x370)!=pc ||
        !ev_object(world,&ev_frame.world) || !ev_object(pc,&ev_frame.controller) ||
        !ev_object(pawn,&ev_frame.pawn) || !ev_object(camera,&ev_frame.camera)) return 0;
    ev_frame.valid=1;
    ev_frame.valid=ev_native_frame_current();
    return ev_frame.valid;
}

#endif
