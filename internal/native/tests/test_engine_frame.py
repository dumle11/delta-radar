"""Native work is gated on the active viewport and an intact possession chain."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[3]
program=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define OFF_UENGINE_GAME_VIEWPORT 0x100
#define OFF_VIEWPORT_WORLD 0x80
typedef struct {uint64_t address;} EVObject;
static unsigned char arena[9][2048];
static uint64_t cached_engine,diagnostic_world,resolved_camera_owner;
static int camera_frame_valid=1,local_player_controlled=1,registered=1;
static int ev_read(uint64_t address,void *out,size_t n) {
 if(address<(uintptr_t)arena || address+n>(uintptr_t)(arena+9))return 0;memcpy(out,(void*)address,n);return 1;
}
static uint64_t ev_ptr(uint64_t address){uint64_t p=0;ev_read(address,&p,8);return p;}
static int ev_object(uint64_t address,EVObject *out){out->address=address;return registered&&address>=(uintptr_t)arena&&address<(uintptr_t)(arena+9);}
static void p(uint64_t address,uint64_t value){memcpy((void*)address,&value,8);}
#include "engine_frame.h"
int main(void) {
 uint64_t engine=(uintptr_t)arena[0],vp=(uintptr_t)arena[1],world=(uintptr_t)arena[2],gi=(uintptr_t)arena[3],locals=(uintptr_t)arena[4],local=(uintptr_t)arena[5],pc=(uintptr_t)arena[6],pawn=(uintptr_t)arena[7],camera=(uintptr_t)arena[8];
 cached_engine=engine;diagnostic_world=world;resolved_camera_owner=camera;
 p(engine+0x100,vp);p(vp+0x80,world);p(world+0x190,gi);p(gi+0x38,locals);p(locals,local);p(local+0x30,pc);p(pc+0x3f0,pawn);p(pc+0x408,camera);p(camera+0x370,pc);
 assert(ev_viewport_registered()&&ev_frame_begin()&&ev_native_frame_current());
 p(vp+0x80,world+1);assert(ev_viewport_registered()&&!ev_native_frame_current()&&!ev_frame_begin()); // Old objects can remain registered.
 p(vp+0x80,world);assert(ev_frame_begin());p(pc+0x3f0,0);assert(!ev_native_frame_current()&&!ev_frame_begin());
 p(pc+0x3f0,pawn);assert(ev_frame_begin());arena[7][0x95]=2;assert(!ev_native_frame_current()&&!ev_frame_begin());
 arena[7][0x95]=0;assert(ev_frame_begin());arena[8][0x95]=2;assert(!ev_native_frame_current());
 arena[8][0x95]=0;registered=0;assert(!ev_viewport_registered()&&!ev_frame_begin());registered=1;camera_frame_valid=0;assert(ev_frame_begin()); // Position recovery is independent of protected camera POV bytes.
 camera_frame_valid=1;local_player_controlled=0;assert(!ev_frame_begin());
 puts("PASS: viewport travel, possession loss, actor/camera destruction, unregistered objects and invalid camera");
}
'''
with tempfile.TemporaryDirectory() as d:
 src=Path(d)/'frame.c';binary=Path(d)/'frame';src.write_text(program)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-I',str(root/'internal/native'),str(src),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
