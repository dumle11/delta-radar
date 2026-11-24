"""Actual named-socket fallback: no guessed points or publication after travel."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
typedef struct {uint64_t address;} EVObject;
typedef struct {uint64_t data;int32_t count,capacity;} SkeletonArray;
typedef struct {uint64_t root,mesh,asset;SkeletonArray info;} SkeletonState;
typedef struct {int16_t aim_indices[3],parents[4];} SkeletonHierarchy;
#define SKELETON_BODY_COUNT 3
static int16_t skeleton_rig_parents[]={-1,0,1,2};
static int16_t skeleton_body_indices[]={1,2,3},skeleton_body_parents[]={-1,0,1};
typedef struct {int dead,position_plaintext,has_bones,bone_count;unsigned aim_bone_mask;float x,y,z,aim_bone_pos[3][3],bone_pos[3][3];int aim_bone_index[3],bone_parent[3];} Entity;
static unsigned char objects[5][4096];
static int frame=1,registered=1,calls,invalid_after,nonfinite,bad_link;
static int ev_read(uint64_t p,void*out,size_t n){if(p<(uintptr_t)objects||p+n>(uintptr_t)(objects+5))return 0;memcpy(out,(void*)p,n);return 1;}
static uint64_t ev_ptr(uint64_t p){uint64_t v=0;ev_read(p,&v,8);return v;}
static void p64(uint64_t p,uint64_t v){memcpy((void*)p,&v,8);}
static int ev_native_frame_current(void){return frame;}
static int ev_same(const EVObject *o){return registered&&o->address>=(uintptr_t)objects&&o->address<(uintptr_t)(objects+5);}
static int ev_object(uint64_t p,EVObject *o){o->address=p;return ev_same(o);}
static int ev_actor_present(uint64_t p){return p==(uintptr_t)objects[0];}
static int skeleton_profile_supported(void){return 1;}
static int skeleton_array_valid(const SkeletonArray*a){return a->data==(uintptr_t)objects[4]&&a->count==4&&a->capacity>=4;}
static const SkeletonHierarchy *skeleton_hierarchy(const SkeletonState*s){(void)s;static SkeletonHierarchy h={{1,2,3},{-1,0,1,2}};return &h;}
static int en_bind_return(uint64_t object,const char*owner,const char*name,uint16_t size,uint16_t offset,uint64_t exec,EVObject*out){assert(object==(uintptr_t)objects[2]&&!strcmp(owner,"SceneComponent")&&!strcmp(name,"GetSocketLocation")&&size==20&&offset==8&&exec==0x1ba7e4c0);out->address=(uintptr_t)objects[4];return 1;}
static void event(void*object,void*function,void*params){
 (void)object;(void)function;uint32_t name;memcpy(&name,params,4);float point[]={1000,2000,3000+(float)name};
 if(nonfinite)point[0]=NAN;memcpy((char*)params+8,point,12);calls++;
 if(invalid_after)frame=0;if(bad_link)p64((uintptr_t)objects[0]+0x3d0,0);
}
static void(*ev_pe_fn)(void*,void*,void*)=event;
#include "engine_joints.h"
int main(void){
 uint64_t actor=(uintptr_t)objects[0],root=(uintptr_t)objects[1],mesh=(uintptr_t)objects[2],asset=(uintptr_t)objects[3],info=(uintptr_t)objects[4];
 p64(actor+0x180,root);p64(actor+0x3d0,mesh);p64(mesh+0x108,root);p64(mesh+0x718,asset);
 SkeletonArray array={info,4,4};memcpy((void*)(asset+0x1c8),&array,sizeof(array));
 for(int i=0;i<4;i++){uint32_t name=i+10;memcpy((void*)(info+i*12),&name,4);}
 Entity e={.position_plaintext=1,.x=10,.y=20,.z=30};
 internal_entity_joints(&e,actor,root);assert(e.aim_bone_mask==7&&e.has_bones&&e.bone_count==3&&calls==6);
 assert(e.aim_bone_index[0]==1&&fabsf(e.aim_bone_pos[0][2]-30.11f)<.001f);
 int before=calls;internal_entity_joints(&e,actor,root);assert(calls==before); // CPU/native complete pose is retained.
 e.aim_bone_mask=0;e.has_bones=0;invalid_after=1;internal_entity_joints(&e,actor,root);assert(calls==before+1&&!e.aim_bone_mask&&!e.has_bones);invalid_after=0;frame=1;
 bad_link=1;internal_entity_joints(&e,actor,root);assert(!e.aim_bone_mask&&!e.has_bones);bad_link=0;p64(actor+0x3d0,mesh);
 nonfinite=1;internal_entity_joints(&e,actor,root);assert(!e.aim_bone_mask&&!e.has_bones);nonfinite=0;
 e.position_plaintext=0;before=calls;internal_entity_joints(&e,actor,root);assert(calls==before);
 e.position_plaintext=1;registered=0;internal_entity_joints(&e,actor,root);assert(calls==before);
 puts("Native joints: named positions, supported rig, existing-pose preservation, protected-center/nonfinite rejection and travel/identity invalidation passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c';binary=Path(d)/'test';c.write_text(source)
    subprocess.run(['cc','-std=c11','-I',str(root),str(c),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
