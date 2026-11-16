#ifndef INTERNAL_AIM_TARGET_H
#define INTERNAL_AIM_TARGET_H
/* Semantic joints resolved from each asset's FNames, never drawing-list indices. */
static int aim_entity_point(const Entity *e,int region,float out[3]) {
    if(region<0 || region>2 || !(e->aim_bone_mask&(1u<<region))) return 0;
    float distance=0;const float center[]={e->x,e->y,e->z};
    for(int j=0;j<3;j++) {
        float value=e->aim_bone_pos[region][j],delta=value-center[j];
        if(!isfinite(value)) return 0;distance+=delta*delta;
    }
    if(distance>9) return 0;
    for(int j=0;j<3;j++) out[j]=e->aim_bone_pos[region][j]*100;
    return 1;
}
typedef int (*AimPointTrace)(void *,const float *);
static int aim_visible_entity_point(const Entity *e,int region,float out[3],AimPointTrace trace,void *context) {
    if(!aim_entity_point(e,region,out)) return 0;
    int result=trace(context,out);
    if(result==0) return 1;
    if(result!=1) return 0;
    int joints=e->has_bones && e->bone_count>0 && e->bone_count<=MAX_SKELETON_BONES?e->bone_count:0;
    for(int k=0;k<3+joints;k++) {
        float point[3];
        if(k<3) {if(k==region || !aim_entity_point(e,k,point)) continue;}
        else {for(int j=0;j<3;j++) point[j]=e->bone_pos[k-3][j]*100;}
        if(trace(context,point)==0) {memcpy(out,point,sizeof(point));return 1;}
    }
    return 0;
}
#endif
