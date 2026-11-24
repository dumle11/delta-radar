#ifndef INTERNAL_ENGINE_CAMERA_H
#define INTERNAL_ENGINE_CAMERA_H
/* Exact matching SDK return-only camera methods. Registered native getters
 * resolve the game's managed location representation; raw cache bytes do not. */
static EVObject ec_location_fn,ec_rotation_fn,ec_fov_fn;
static int ec_stage;
static unsigned ec_samples;
static uint64_t ec_manager,ec_world;
static _Atomic int64_t ec_presented_at;
/* Scanner-owned decoded sample, shared with its own text projection pass. */
static float ec_values[7];
static int64_t ec_sampled_at;
static void ec_note_present(void) { atomic_store(&ec_presented_at,ef_now()); }
static void internal_position_frame_begin(void) { ev_frame_begin(); }
static int en_bind_return(uint64_t manager,const char *owner,const char *name,uint16_t size,uint16_t result_offset,uint64_t exec,EVObject *out) {
    if(out->address && ev_same(out)) return 1;
    memset(out,0,sizeof(*out));
    for(uint64_t cls=ev_ptr(manager+8),depth=0;is_any_ptr(cls) && depth<32;cls=ev_ptr(cls+0x40),depth++) {
        char type[80]={0};resolve_fname(read_i32(cls+OFF_UOBJECT_NAME),type,sizeof(type));
        if(strcmp(type,owner)) continue;
        for(uint64_t field=ev_ptr(cls+0x50),n=0;is_any_ptr(field) && n<512;field=ev_ptr(field+0x28),n++) {
            char method[80]={0};resolve_fname(read_i32(field+OFF_UOBJECT_NAME),method,sizeof(method));
            if(strcmp(method,name) || ev_ptr(field+0x10)!=cls) continue;
            uint16_t actual=0,result=0;uint32_t flags=0;
            return ev_read(field+0xb2,&actual,2) && actual==size &&
                ev_read(field+0xb4,&result,2) && result==result_offset &&
                ev_read(field+0xb8,&flags,4) && (flags&0x400) &&
                ev_ptr(field+0xd8)==ue4_base+exec && ev_object(field,out);
        }
        return 0;
    }
    return 0;
}
static int internal_camera_sample(uint64_t controller,float out[7]) {
    ec_stage=1;
    if(!ev_pe_fn || !ev_native_frame_current() || controller!=ev_frame.controller.address) return 0;
    uint64_t manager=ev_frame.camera.address;
    EVObject camera=ev_frame.camera;
    ec_stage=2;
    if(!ev_same(&camera) || ev_ptr(controller+0x408)!=manager || ev_ptr(manager+0x370)!=controller) return 0;
    ec_stage=3;
    if(!en_bind_return(manager,"PlayerCameraManager","GetCameraLocation",12,0,0x1ba55d6c,&ec_location_fn) ||
       !en_bind_return(manager,"PlayerCameraManager","GetCameraRotation",12,0,0x1ba55dac,&ec_rotation_fn) ||
       !en_bind_return(manager,"PlayerCameraManager","GetFOVAngle",4,0,0x1ba55dec,&ec_fov_fn)) return 0;
    float values[7]={NAN,NAN,NAN,NAN,NAN,NAN,NAN};
    ev_pe_fn((void*)(uintptr_t)manager,(void*)(uintptr_t)ec_location_fn.address,values);
    if(!ev_native_frame_current() || !ev_same(&camera)) return 0;
    ev_pe_fn((void*)(uintptr_t)manager,(void*)(uintptr_t)ec_rotation_fn.address,values+3);
    if(!ev_native_frame_current() || !ev_same(&camera)) return 0;
    ev_pe_fn((void*)(uintptr_t)manager,(void*)(uintptr_t)ec_fov_fn.address,values+6);
    ec_stage=4;
    if(!ev_native_frame_current() || !ev_same(&camera) || ev_ptr(controller+0x408)!=manager ||
       ev_ptr(manager+0x370)!=controller) return 0;
    for(int i=0;i<7;i++) if(!isfinite(values[i]) || fabsf(values[i])>10000000) return 0;
    if((values[0]==0 && values[1]==0 && values[2]==0) || values[6]<5 || values[6]>=180) return 0;
    /* Use observed game presentation, not a guessed/raw cache timestamp.
     * A stationary camera remains valid; backgrounded rendering expires. */
    int64_t now=ef_now(),presented=atomic_load(&ec_presented_at);ec_stage=5;
    if(!presented || now-presented>500 || now<presented) return 0;
    memcpy(out,values,sizeof(values));memcpy(ec_values,values,sizeof(values));
    ec_sampled_at=now;ec_manager=manager;ec_world=ev_frame.world.address;resolved_camera_owner=manager;
    ec_stage=0;ec_samples++;return 1;
}
static int ec_snapshot(float out[6],float *roll) {
    if(ec_stage || !ec_samples || ef_now()-ec_sampled_at>200 || !ev_native_frame_current() ||
       ec_world!=ev_frame.world.address || ec_manager!=ev_frame.camera.address) return 0;
    for(int i=0;i<3;i++) out[i]=ec_values[i]/100;
    out[3]=ec_values[4];out[4]=ec_values[3];out[5]=ec_values[6];*roll=ec_values[5];return 1;
}
#endif
