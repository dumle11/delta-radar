#ifndef INTERNAL_RENDER_CAMERA_H
#define INTERNAL_RENDER_CAMERA_H
/* Resample a coherent camera POV immediately before projection, after item
 * work and physics queries. Never substitute controller/body rotation. */
static int render_camera_snapshot(float out[6],float *roll) {
    uint64_t camera=resolved_camera_owner;
    if(!camera_frame_valid || !local_player_controlled || resolved_camera_cache_off<0) return 0;
    EVObject object;
    if(!ev_object(camera,&object)) return 0;
    uint64_t controller=ev_ptr(camera+0x370),pawn=atomic_load(&ev_local_pawn);
    if(!pawn || ev_ptr(controller+0x408)!=camera || ev_ptr(controller+0x3f0)!=pawn) return 0;
    unsigned char a[0x30],b[0x30];uint64_t cache=camera+(uint64_t)resolved_camera_cache_off;
    if(!ev_read(cache,a,sizeof(a)) || !ev_read(cache,b,sizeof(b)) || memcmp(a,b,sizeof(a)) ||
        !enc_handler_unmanaged(a+0x1c) || !ev_same(&object)) return 0;
    float position[3],rotation[3],fov,locked;
    memcpy(position,a+0x10,sizeof(position));memcpy(rotation,a+0x20,sizeof(rotation));memcpy(&fov,a+0x2c,4);
    if(ev_read(camera+0x38c,&locked,4) && isfinite(locked) && locked>=10 && locked<180) fov=locked;
    if(!isfinite(fov) || fov<10 || fov>=180) return 0;
    for(int j=0;j<3;j++) if(!isfinite(position[j]) || fabsf(position[j])>10000000 ||
        !isfinite(rotation[j]) || fabsf(rotation[j])>360) return 0;
    for(int j=0;j<3;j++) out[j]=position[j]/100;
    out[3]=rotation[1];out[4]=rotation[0];out[5]=fov;*roll=rotation[2];return 1;
}
#endif
