#define _GNU_SOURCE
#define __ANDROID_API__ 31
/* Full reader/wire fixture using only this process's synthetic memory. */
#define main scanner_main
#include "../memscan.c"
#undef main
#include <assert.h>
static void ptr(void *p,const void *v) { uint64_t a=(uintptr_t)v;memcpy(p,&a,8); }
static void i32(void *p,int32_t v) { memcpy(p,&v,4); }
static void array(void *p,void *data,int count) { SkeletonArray a={(uintptr_t)data,count,count};memcpy(p,&a,sizeof(a)); }
static void near(float a,float b) { assert(fabsf(a-b)<0.0001f); }
int main(void) {
    unsigned char actor[0x400]={0},root[0x280]={0},mesh[0x800]={0},asset[0x220]={0},cls[0x60]={0};
    unsigned char info[80*12]={0};
    uint32_t getter[]={0xf9438c08,0xb4000068,0xb941d100,0xd65f03c0};
    game_pid=getpid();use_vm_readv=1;ue4_base=(uintptr_t)getter-0x1acda0fc;
    ptr(actor+0x180,root);ptr(actor+0x3d0,mesh);ptr(mesh+0x108,root);ptr(mesh+0x718,asset);ptr(asset+8,cls);
    for(int j=0;j<75;j++)i32(info+j*12+8,skeleton_rig_parents[j]);
    for(int j=75;j<80;j++)i32(info+j*12+8,0);
    array(asset+0x1c8,info,80);
    SkeletonTransform rt={.rotation={0,0,0.70710678118f,0.70710678118f},.translation={999999,999999,999999},.scale={2,3,1}};
    SkeletonTransform mt={.rotation={0,0,1,0},.translation={999999,999999,999999},.scale={1,2,3}};
    rt.padding[0]=rt.padding[1]=mt.padding[0]=mt.padding[1]=255;
    memcpy(root+0x210,&rt,48);memcpy(mesh+0x210,&mt,48);
    float relative[]={10,20,-86};memcpy(mesh+0x168,relative,12);mesh[0x174]=mesh[0x175]=255;
    SkeletonTransform poses[2][80]={0};
    for(int b=0;b<2;b++)for(int j=0;j<80;j++){
        poses[b][j].rotation[3]=1;
        for(int k=0;k<3;k++)poses[b][j].scale[k]=1;
    }
    for(int b=0;b<2;b++){poses[b][4].translation[0]=10+b*10;poses[b][4].translation[1]=20;poses[b][4].translation[2]=30;}
    array(mesh+0x750,poses[0],80);array(mesh+0x760,poses[1],80);
    Entity e={.x=10,.y=20,.z=30,.position_plaintext=1};strcpy(e.kind,"player");strcpy(e.id,"ent_1");
    read_entity_bones(&e,(uintptr_t)actor,(uintptr_t)root);
    assert(e.has_bones&&e.bone_count==17);assert(e.bone_parent[2]==1);
    near(e.bone_pos[1][0],9.30f);near(e.bone_pos[1][1],19.80f);near(e.bone_pos[1][2],30.04f);
    i32(mesh+0x794,1);read_entity_bones(&e,(uintptr_t)actor,(uintptr_t)root);
    assert(e.has_bones);near(e.bone_pos[1][0],9.20f);
    int sockets[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)==0);
    entities[0]=e;num_entities=1;persistent_sock=sockets[0];assert(post_to_radar()==0);
    char payload[8192];ssize_t got=read(sockets[1],payload,sizeof(payload)-1);assert(got>0);payload[got]=0;
    assert(strstr(payload,"\"bonesVerified\":true,\"boneCount\":17"));
    assert(strstr(payload,"\"boneParents\":[-1,0,1,2,3,1,5,6"));close_radar_sock();close(sockets[1]);
    i32(mesh+0x794,2);read_entity_bones(&e,(uintptr_t)actor,(uintptr_t)root);assert(!e.has_bones&&!e.bone_count);
    i32(mesh+0x794,1);mesh[0x174]=0;read_entity_bones(&e,(uintptr_t)actor,(uintptr_t)root);assert(!e.has_bones);
    mesh[0x174]=255;i32(mesh+0x748,1);read_entity_bones(&e,(uintptr_t)actor,(uintptr_t)root);assert(!e.has_bones);
    i32(mesh+0x748,0);poses[1][4].translation[0]=NAN;read_entity_bones(&e,(uintptr_t)actor,(uintptr_t)root);assert(!e.has_bones);
    poses[1][4].translation[0]=20;e.position_plaintext=0;read_entity_bones(&e,(uintptr_t)actor,(uintptr_t)root);assert(!e.has_bones);
    puts("skeleton reader: active buffers, 3D attachment, parent graph, wire payload and invalid-state rejection passed");return 0;
}
