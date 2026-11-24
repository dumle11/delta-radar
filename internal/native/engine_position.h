#ifndef INTERNAL_ENGINE_POSITION_H
#define INTERNAL_ENGINE_POSITION_H
/* Internal read-only SDK getters handle the game's registered location data.
 * Do not guess a codec/provider or call a pointer found inside encrypted data. */
static EVObject ep_actor_fn,ep_component_fn;
static int ep_stage;
static unsigned ep_native_calls,ep_native_returns;
static void ep_dispatch(uint64_t object,const EVObject *function,float out[3],const char *kind) {
    unsigned call=++ep_native_calls;
    if(call<=4) {char text[96];snprintf(text,sizeof(text),"position SDK enter: %s call=%u",kind,call);_diag(text);}
    ev_pe_fn((void *)(uintptr_t)object,(void *)(uintptr_t)function->address,out);
    ++ep_native_returns;
    if(call<=4) {char text[96];snprintf(text,sizeof(text),"position SDK return: %s call=%u",kind,call);_diag(text);}
}
static int ep_bind(uint64_t object,const char *owner,const char *name,uint64_t exec,EVObject *fn) {
    if(fn->address && ev_same(fn)) return 1;
    memset(fn,0,sizeof(*fn));
    for(uint64_t cls=ev_ptr(object+8),depth=0;is_any_ptr(cls) && depth<32;cls=ev_ptr(cls+0x40),depth++) {
        char type[80]={0};resolve_fname(read_i32(cls+OFF_UOBJECT_NAME),type,sizeof(type));
        if(strcmp(type,owner)) continue;
        for(uint64_t field=ev_ptr(cls+0x50),n=0;is_any_ptr(field) && n<512;field=ev_ptr(field+0x28),n++) {
            char method[80]={0};resolve_fname(read_i32(field+OFF_UOBJECT_NAME),method,sizeof(method));
            if(strcmp(method,name) || ev_ptr(field+0x10)!=cls) continue;
            uint16_t size=0,result=0;uint32_t flags=0,code[4];
            const uint32_t signature[]={0xf81e0ff3,0xa9017bfd,0x910043fd,0xf9401028};
            return ev_read(field+0xb2,&size,2) && size==12 &&
                ev_read(field+0xb4,&result,2) && result==0 &&
                ev_read(field+0xb8,&flags,4) && (flags&0x400) &&
                ev_ptr(field+0xd8)==ue4_base+exec &&
                ev_read(ue4_base+exec,code,sizeof(code)) && !memcmp(code,signature,sizeof(code)) && ev_object(field,fn);
        }
        return 0;
    }
    return 0;
}
static int internal_actor_position(uint64_t actor,uint64_t root,float out[3]) {
    ep_stage=1;
    if(!ev_pe_fn) {ep_stage=10;return 0;}
    if(!ev_native_frame_current()) {ep_stage=11;return 0;}
    if(ev_frame.world.address!=diagnostic_world) {ep_stage=12;return 0;}
    if(!ev_actor_present(actor)) {ep_stage=13;return 0;}
    if(ev_ptr(actor+0x180)!=root) {ep_stage=14;return 0;}
    EVObject actor_ref,root_ref;
    ep_stage=2;
    if(!ev_object(actor,&actor_ref) || !ev_object(root,&root_ref) ||
        !ew_is_a(root,"SceneComponent",0x240)) return 0;
    ep_stage=3;
    if(!ep_bind(actor,"Actor","K2_GetActorLocation",0x11904f88,&ep_actor_fn) ||
        !ep_bind(root,"SceneComponent","K2_GetComponentLocation",0x11326e5c,&ep_component_fn)) return 0;
    float component[3]={NAN,NAN,NAN},location[3]={NAN,NAN,NAN};
    ep_stage=4;
    ep_dispatch(root,&ep_component_fn,component,"component");
    if(!ev_native_frame_current() || !ev_same(&actor_ref) || !ev_same(&root_ref)) return 0;
    ep_dispatch(actor,&ep_actor_fn,location,"actor");
    ep_stage=5;
    if(!ev_native_frame_current() || !ev_same(&actor_ref) || !ev_same(&root_ref) || ev_ptr(actor+0x180)!=root) return 0;
    float distance=0;
    for(int j=0;j<3;j++) {
        if(!isfinite(component[j]) || !isfinite(location[j]) || fabsf(location[j])>10000000) return 0;
        float delta=component[j]-location[j];distance+=delta*delta;
    }
    if(distance>50*50 || (location[0]==0 && location[1]==0 && location[2]==0)) return 0;
    memcpy(out,location,sizeof(location));ep_stage=0;return 1;
}
#endif
