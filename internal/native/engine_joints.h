#ifndef INTERNAL_ENGINE_JOINTS_H
#define INTERNAL_ENGINE_JOINTS_H
/* Recover actual named joints when protected component transforms prevent
 * composing the CPU pose. The bone names still come from the asset hierarchy. */
static EVObject ej_socket_fn;
static unsigned ej_recovered,ej_failed;
typedef struct {uint32_t name[2];float point[3];} EJSocketParams;
_Static_assert(sizeof(EJSocketParams)==0x14,"socket parameters");
_Static_assert(offsetof(EJSocketParams,point)==8,"socket return");
static int ej_point(const EVObject *mesh_ref,const SkeletonState *state,int index,const float center[3],float point[3]) {
    if(index<0 || index>=state->info.count || !ev_native_frame_current() || !ev_same(mesh_ref) || !ev_same(&ej_socket_fn)) return 0;
    EJSocketParams params={.point={NAN,NAN,NAN}};
    if(!ev_read(state->info.data+(uint64_t)index*12,params.name,8) || !params.name[0]) return 0;
    ev_pe_fn((void*)(uintptr_t)mesh_ref->address,(void*)(uintptr_t)ej_socket_fn.address,&params);
    float distance=0;
    for(int j=0;j<3;j++) {
        point[j]=params.point[j]/100;float delta=point[j]-center[j];
        if(!isfinite(point[j])) return 0;distance+=delta*delta;
    }
    return distance<=9;
}
static void internal_entity_joints(Entity *e,uint64_t actor,uint64_t root) {
    if(e->dead || !e->position_plaintext || (e->aim_bone_mask==7 && e->has_bones) ||
       !ev_pe_fn || !ev_native_frame_current() || !skeleton_profile_supported()) return;
    SkeletonState state={.root=root};
    state.mesh=ev_ptr(actor+0x3d0);state.asset=ev_ptr(state.mesh+0x718);
    EVObject actor_ref,root_ref,mesh_ref,asset_ref;
    if(ev_ptr(actor+0x180)!=root || ev_ptr(state.mesh+0x108)!=root ||
       !ev_actor_present(actor) || !ev_object(actor,&actor_ref) || !ev_object(root,&root_ref) ||
       !ev_object(state.mesh,&mesh_ref) || !ev_object(state.asset,&asset_ref) ||
       !ev_read(state.asset+0x1c8,&state.info,sizeof(state.info)) || !skeleton_array_valid(&state.info)) return;
    const SkeletonHierarchy *hierarchy=skeleton_hierarchy(&state);
    if(!hierarchy || !en_bind_return(state.mesh,"SceneComponent","GetSocketLocation",20,8,0x1ba7e4c0,&ej_socket_fn)) return;
    float center[]={e->x,e->y,e->z},semantic[3][3],body[SKELETON_BODY_COUNT][3];
    unsigned mask=0;
    for(int region=0;region<3;region++) if(!(e->aim_bone_mask&(1u<<region)) &&
        ej_point(&mesh_ref,&state,hierarchy->aim_indices[region],center,semantic[region])) mask|=1u<<region;
    int body_ok=!e->has_bones && state.info.count>=(int)(sizeof(skeleton_rig_parents)/sizeof(*skeleton_rig_parents)) &&
        !memcmp(hierarchy->parents,skeleton_rig_parents,sizeof(skeleton_rig_parents));
    if(body_ok) for(int i=0;i<SKELETON_BODY_COUNT;i++)
        if(!ej_point(&mesh_ref,&state,skeleton_body_indices[i],center,body[i])) {body_ok=0;break;}
    SkeletonArray after;
    if(!ev_native_frame_current() || !ev_same(&actor_ref) || !ev_same(&root_ref) ||
       !ev_same(&mesh_ref) || !ev_same(&asset_ref) || !ev_actor_present(actor) ||
       ev_ptr(actor+0x180)!=root || ev_ptr(actor+0x3d0)!=state.mesh || ev_ptr(state.mesh+0x108)!=root ||
       ev_ptr(state.mesh+0x718)!=state.asset || !ev_read(state.asset+0x1c8,&after,sizeof(after)) ||
       memcmp(&after,&state.info,sizeof(after))) {ej_failed++;return;}
    for(int region=0;region<3;region++) if(mask&(1u<<region)) {
        memcpy(e->aim_bone_pos[region],semantic[region],sizeof(semantic[region]));
        e->aim_bone_index[region]=hierarchy->aim_indices[region];
    }
    e->aim_bone_mask|=mask;
    if(body_ok) {
        for(int i=0;i<SKELETON_BODY_COUNT;i++) {
            memcpy(e->bone_pos[i],body[i],sizeof(body[i]));e->bone_parent[i]=skeleton_body_parents[i];
        }
        e->bone_count=SKELETON_BODY_COUNT;e->has_bones=1;
    }
    if(mask || body_ok) ej_recovered++;else ej_failed++;
}
#endif
