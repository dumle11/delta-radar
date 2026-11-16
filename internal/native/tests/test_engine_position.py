"""SDK location binding, independent getter agreement and receiver lifetime checks."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[3]
program=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#define OFF_UOBJECT_NAME 0x1c
typedef struct {uint64_t address;} EVObject;
static unsigned char objects[6][1024];static uint64_t ue4_base=0x100000000ULL,diagnostic_world=123;
static struct {EVObject world;} ev_frame={{123}};
static int frame=1,alive=1,registered=1,type_ok=1,calls,move_root,disagree,nonfinite;
static void put64(uint64_t p,uint64_t v){memcpy((void*)p,&v,8);}static void put32(uint64_t p,uint32_t v){memcpy((void*)p,&v,4);}static void put16(uint64_t p,uint16_t v){memcpy((void*)p,&v,2);}
static int ev_read(uint64_t p,void *out,size_t n) {
 if(p==ue4_base+0x11904f88 || p==ue4_base+0x11326e5c) {uint32_t code[]={0xf81e0ff3,0xa9017bfd,0x910043fd,0xf9401028};memcpy(out,code,n);return 1;}
 if(p<(uintptr_t)objects || p+n>(uintptr_t)(objects+6))return 0;memcpy(out,(void*)p,n);return 1;
}
static uint64_t ev_ptr(uint64_t p){uint64_t v=0;ev_read(p,&v,8);return v;}static int32_t read_i32(uint64_t p){int32_t v=0;ev_read(p,&v,4);return v;}
static int is_any_ptr(uint64_t p){return p>=(uintptr_t)objects&&p<(uintptr_t)(objects+6);}
static void resolve_fname(int id,char *out,size_t n){const char *names[]={"","Actor","SceneComponent","K2_GetActorLocation","K2_GetComponentLocation"};snprintf(out,n,"%s",id>0&&id<5?names[id]:"");}
static int ev_same(const EVObject *o){return registered&&is_any_ptr(o->address);}static int ev_object(uint64_t p,EVObject *o){o->address=p;return ev_same(o);}
static int ev_actor_present(uint64_t p){(void)p;return alive;}static int ev_native_frame_current(void){return frame;}
static int ew_is_a(uint64_t p,const char *type,uint32_t n){(void)p;(void)type;(void)n;return type_ok;}
static void event(void *object,void *function,void *params){(void)object;(void)function;float *out=params;calls++;out[0]=nonfinite?NAN:1000;out[1]=2000;out[2]=3000;if(disagree&&calls%2==0)out[0]+=1000;if(move_root)put64((uintptr_t)objects[0]+0x180,0);}
static void (*ev_pe_fn)(void*,void*,void*)=event;
static void _diag(const char *text){(void)text;}
#include "engine_position.h"
int main(void){
 uint64_t actor=(uintptr_t)objects[0],component=(uintptr_t)objects[1],ac=(uintptr_t)objects[2],cc=(uintptr_t)objects[3],af=(uintptr_t)objects[4],cf=(uintptr_t)objects[5];
 put64(actor+8,ac);put64(component+8,cc);put64(actor+0x180,component);put32(ac+0x1c,1);put32(cc+0x1c,2);put64(ac+0x50,af);put64(cc+0x50,cf);
 put32(af+0x1c,3);put32(cf+0x1c,4);put64(af+0x10,ac);put64(cf+0x10,cc);put16(af+0xb2,12);put16(cf+0xb2,12);put32(af+0xb8,0x400);put32(cf+0xb8,0x400);put64(af+0xd8,ue4_base+0x11904f88);put64(cf+0xd8,ue4_base+0x11326e5c);
 float out[3];assert(internal_actor_position(actor,component,out)&&calls==2&&out[0]==1000);
 assert(ep_native_calls==2&&ep_native_returns==2);
 frame=0;assert(!internal_actor_position(actor,component,out)&&calls==2);frame=1;
 disagree=1;assert(!internal_actor_position(actor,component,out));disagree=0;nonfinite=1;assert(!internal_actor_position(actor,component,out));nonfinite=0;
 move_root=1;assert(!internal_actor_position(actor,component,out));move_root=0;put64(actor+0x180,component);
 registered=0;assert(!internal_actor_position(actor,component,out));registered=1;
 memset(&ep_actor_fn,0,sizeof(ep_actor_fn));put16(af+0xb2,16);assert(!internal_actor_position(actor,component,out));put16(af+0xb2,12);
 put64(af+0xd8,ue4_base+1);assert(!internal_actor_position(actor,component,out));
 puts("PASS: exact SDK binding, current world, getter agreement, finite output, receiver/owner lifetime, parameter ABI and exec identity");
}
'''
with tempfile.TemporaryDirectory() as d:
 src=Path(d)/'position.c';binary=Path(d)/'position';src.write_text(program)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-I',str(root/'internal/native'),str(src),'-lm','-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
