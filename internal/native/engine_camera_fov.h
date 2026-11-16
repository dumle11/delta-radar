#ifndef INTERNAL_ENGINE_CAMERA_FOV_H
#define INTERNAL_ENGINE_CAMERA_FOV_H
/* Engine GetFOVAngle (0x115be928) first reads LockedFOV at +0x38c;
 * positive values override the camera pipeline. Never race its output caches. */
typedef struct { EFRef camera, controller; float before, applied; int active; } FOVSession;
static FOVSession fov_session;
static int fov_supported(void) {
    uint32_t code[3], expected[]={0xbd438c00,0x1e202008,0x540001ac};
    return ev_read(ue4_base+0x115be928,code,sizeof(code)) && !memcmp(code,expected,sizeof(code));
}
static int fov_owned(const FOVSession *s) {
    return ef_same(&s->camera) && ef_same(&s->controller) &&
        ev_ptr(s->camera.object.address+0x370)==s->controller.object.address &&
        ev_ptr(s->controller.object.address+0x408)==s->camera.object.address;
}
static void fov_release(void) {
    FOVSession *s=&fov_session; float current;
    if(s->active && fov_owned(s) && ev_read(s->camera.object.address+0x38c,&current,4) && current==s->applied)
        ew_write(s->camera.object.address+0x38c,&s->before,4);
    memset(s,0,sizeof(*s));
}
static void fov_publish(uint64_t camera,int target) {
    FOVSession next={0};
    uint64_t controller=ev_ptr(camera+0x370);
    int valid=target>=10 && target<=200 && fov_supported() &&
        ew_is_a(camera,"PlayerCameraManager",0x4380) && ef_ref(camera,&next.camera) &&
        ef_ref(controller,&next.controller) && fov_owned(&next);
    if(fov_session.active && (!valid || !fov_owned(&fov_session) ||
        next.camera.object.address!=fov_session.camera.object.address || next.camera.serial!=fov_session.camera.serial)) fov_release();
    if(!valid) return;
    if(!fov_session.active) {
        if(!ev_read(camera+0x38c,&next.before,4) || !isfinite(next.before) || next.before < -1 || next.before>200) return;
        fov_session=next;
    }
    float value=(float)target;
    if(ew_write(camera+0x38c,&value,4)) {
        fov_session.applied=value;fov_session.active=1;
        static int64_t next_report;static int previous;
        if(previous!=target || ef_now()>=next_report) {
            float cache=0,view=0;ev_read(camera+0x2dcc,&cache,4);ev_read(camera+0x18cc,&view,4);
            char text[130];snprintf(text,sizeof(text),"camera FOV: target=%d lock=%.1f cache=%.1f view=%.1f",target,value,cache,view);_diag(text);
            previous=target;next_report=ef_now()+5000;
        }
    }
}
#endif
