"""Production camera binding: protected-POV independence and lifetime rejection."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <assert.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define OFF_UOBJECT_NAME 0x1c
#define OFF_CAMERA_CACHE_PRIVATE 0x2da0
typedef struct {uint64_t address;} EVObject;
static unsigned char objects[6][16384];
static uint64_t ue4_base=0x100000000,resolved_camera_owner;
static struct {EVObject camera,controller,world;} ev_frame;
static int frame=1,registered=1,calls,invalidate,nonfinite;
static int64_t now=1000;
static int64_t ef_now(void){return now;}
static int ev_read(uint64_t p,void *out,size_t n){if(p<(uintptr_t)objects||p+n>(uintptr_t)(objects+6))return 0;memcpy(out,(void*)p,n);return 1;}
static uint64_t ev_ptr(uint64_t p){uint64_t v=0;ev_read(p,&v,8);return v;}
static int32_t read_i32(uint64_t p){int32_t v=0;ev_read(p,&v,4);return v;}
static int is_any_ptr(uint64_t p){return p>=(uintptr_t)objects&&p<(uintptr_t)(objects+6);}
static int ev_same(const EVObject *o){return registered&&is_any_ptr(o->address);}
static int ev_object(uint64_t p,EVObject *o){o->address=p;return ev_same(o);}
static int ev_native_frame_current(void){return frame;}
static int ev_frame_begin(void){return frame;}
static void resolve_fname(int id,char *out,size_t n){const char *names[]={"","PlayerCameraManager","GetCameraLocation","GetCameraRotation","GetFOVAngle"};snprintf(out,n,"%s",id>0&&id<5?names[id]:"");}
static void put64(uint64_t p,uint64_t v){memcpy((void*)p,&v,8);}
static void put32(uint64_t p,uint32_t v){memcpy((void*)p,&v,4);}
static void put16(uint64_t p,uint16_t v){memcpy((void*)p,&v,2);}
static void event(void *object,void *function,void *params){
 (void)object;calls++;float *out=params;
 if(function==objects[3]){out[0]=nonfinite?NAN:1000;out[1]=2000;out[2]=3000;}
 else if(function==objects[4]){out[0]=10;out[1]=20;out[2]=30;}
 else if(function==objects[5])out[0]=95;
 if(invalidate)frame=0;
}
static void (*ev_pe_fn)(void*,void*,void*)=event;
#include "engine_camera.h"
int main(void){
 uint64_t pc=(uintptr_t)objects[0],camera=(uintptr_t)objects[1],cls=(uintptr_t)objects[2];
 ev_frame.camera.address=camera;ev_frame.controller.address=pc;ev_frame.world.address=123;
 put64(pc+0x408,camera);put64(camera+0x370,pc);put64(camera+8,cls);put32(cls+0x1c,1);put64(cls+0x50,(uintptr_t)objects[3]);
 uint64_t exec[]={0x1ba55d6c,0x1ba55dac,0x1ba55dec};
 for(int i=3;i<6;i++){uint64_t f=(uintptr_t)objects[i];put32(f+0x1c,i-1);put64(f+0x10,cls);put16(f+0xb2,i==5?4:12);put32(f+0xb8,0x400);put64(f+0xd8,ue4_base+exec[i-3]);if(i<5)put64(f+0x28,(uintptr_t)objects[i+1]);}
 float stamp=12,out[7];memcpy((void*)(camera+OFF_CAMERA_CACHE_PRIVATE),&stamp,4);
 ec_note_present();assert(internal_camera_sample(pc,out)&&calls==3&&out[0]==1000&&out[4]==20&&out[6]==95);
 float projected[6],roll;assert(ec_snapshot(projected,&roll)&&projected[0]==10&&projected[3]==20&&roll==30);
 now+=501;assert(!internal_camera_sample(pc,out)&&ec_stage==5); // No presentation: backgrounded/frozen renderer.
 ec_note_present();assert(internal_camera_sample(pc,out)); // Stationary view / unchanged or unusable raw timestamp is fine.
 stamp=0;memcpy((void*)(camera+OFF_CAMERA_CACHE_PRIVATE),&stamp,4);assert(internal_camera_sample(pc,out));
 nonfinite=1;assert(!internal_camera_sample(pc,out));nonfinite=0;
 invalidate=1;int before=calls;assert(!internal_camera_sample(pc,out)&&calls==before+1);invalidate=0;frame=1;
 put64(pc+0x408,0);assert(!internal_camera_sample(pc,out));put64(pc+0x408,camera);
 registered=0;assert(!internal_camera_sample(pc,out));registered=1;
 memset(&ec_location_fn,0,sizeof(ec_location_fn));put16((uintptr_t)objects[3]+0xb4,8);assert(!internal_camera_sample(pc,out));put16((uintptr_t)objects[3]+0xb4,0);
 put64((uintptr_t)objects[3]+0xd8,ue4_base+1);assert(!internal_camera_sample(pc,out));
 puts("Camera SDK: exact ABI/exec, native values, presentation freshness / raw timestamp independence, nonfinite data and mid-query lifetime changes passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c';binary=Path(d)/'test';c.write_text(source)
    subprocess.run(['cc','-std=c11','-I',str(root),str(c),'-lm','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
