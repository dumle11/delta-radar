"""Production FOV writer rejects retired cameras and invalid destinations."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
visibility = (root / "internal/native/engine_visibility.h").read_text()
helpers = visibility[visibility.index("typedef struct { uint64_t data;"):visibility.index("/* Bind the UGPPhysics")]
weapon = (root / "internal/native/engine_weapon.h").read_text()
writes = weapon[weapon.index("/* Kernel-checked destination copy"):weapon.index("static int ew_owned_by")]
fire = (root / "internal/native/engine_fire.h").read_text()
refs = fire[fire.index("static int64_t ef_now("):fire.index("/* Native UFunctions")]
helpers=helpers.replace("static int ev_read(uint64_t address, void *out, size_t size) {", "static int ev_read(uint64_t address, void *out, size_t size) { if(address==ue4_base+0x115be928 && size==sizeof(fixture_code)) {memcpy(out,fixture_code,size);return 1;}")
program = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <time.h>
#include <sys/mman.h>
#include "self-memory.h"
#define OFF_GUOBJECTARRAY_FROM_BASE 0x206aef48
#define OFF_UOBJECT_NAME 0x1c
static uint64_t ue4_base;
static uint32_t fixture_code[]={0xbd438c00,0x1e202008,0x540001ac};
''' + helpers + r'''
static unsigned char camera[0x5000], controller[0x5000], cls[0x80], array[0x28], items[2][24];
static uint64_t chunks[1];
static __thread SelfMemoryReader ew_writer={-1,-1};
static _Atomic int lib_running_flag;
static int is_any_ptr(uint64_t p) {return p>=65536 && p<0xffffffffffffULL && !(p&3);}
static int32_t read_i32(uint64_t p) {int32_t n=0;ev_read(p,&n,4);return n;}
static void resolve_fname(int32_t n,char *out,size_t size) {snprintf(out,size,"%s",n==10?"PlayerCameraManager":"Other");}
''' + writes + r'''
typedef struct {EVObject object;int32_t serial;} EFRef;
''' + refs + r'''
#define pthread_setname_np(thread,name) ((void)(thread),(void)(name),0)
static void _diag(const char *text) {(void)text;}
#include "engine_camera_fov.h"
static int camera_frame_valid,local_player_controlled,resolved_camera_cache_off;
static uint64_t resolved_camera_owner;
static int enc_handler_unmanaged(const uint8_t *p) {return p[0]==255 && p[1]==255 && !p[2];}
#include "render_camera.h"
static void put32(void *p,int n) {memcpy(p,&n,4);}
static void put64(void *p,uint64_t n) {memcpy(p,&n,8);}
static float getf(void *p) {float n;memcpy(&n,p,4);return n;}
int main(void) {
    ue4_base=(uintptr_t)array-OFF_GUOBJECTARRAY_FROM_BASE;chunks[0]=(uintptr_t)items;
    put32(array+0x14,2);put64(array+0x20,(uintptr_t)chunks);put32(cls+0x1c,10);put32(cls+0x3c,0x4380);
    for(int i=0;i<2;i++) {
        unsigned char *p=i?controller:camera;put64(p+8,(uintptr_t)cls);put32(p+0x1c,20+i);put32(p+0x24,i);
        put64(items[i],(uintptr_t)p);put32(items[i]+0x14,100+i);
    }
    put64(camera+0x370,(uintptr_t)controller);put64(controller+0x408,(uintptr_t)camera);
    float original=-1;memcpy(camera+0x38c,&original,4);
    unsigned char before[sizeof(camera)];memcpy(before,camera,sizeof(camera));
    fov_publish((uintptr_t)camera,120);assert(fov_session.active && getf(camera+0x38c)==120);
    assert(!memcmp(before,camera,0x38c) && !memcmp(before+0x390,camera+0x390,sizeof(camera)-0x390));
    fov_publish((uintptr_t)camera,130);assert(getf(camera+0x38c)==130 && fov_session.before==-1);
    fov_publish(0,0);assert(!fov_session.active && !memcmp(before,camera,sizeof(camera)));
    fov_publish((uintptr_t)camera,120);fixture_code[0]=0;
    fov_publish((uintptr_t)camera,130);assert(!fov_session.active && getf(camera+0x38c)==-1);
    fixture_code[0]=0xbd438c00;
    fov_publish((uintptr_t)camera,120);float changed=100;memcpy(camera+0x38c,&changed,4);
    fov_publish(0,0);assert(getf(camera+0x38c)==100); // Preserve a newer host change.
    memcpy(camera+0x38c,&original,4);fov_publish((uintptr_t)camera,120);
    put32(items[0]+0x14,999);fov_publish(0,0);assert(!fov_session.active && getf(camera+0x38c)==120);
    put32(items[0]+0x14,100);memcpy(camera+0x38c,&original,4);
    fov_publish((uintptr_t)camera,120);put64(controller+0x408,0);
    fov_publish((uintptr_t)camera,130);assert(!fov_session.active && getf(camera+0x38c)==120);
    put64(controller+0x408,(uintptr_t)camera);
    camera_frame_valid=local_player_controlled=1;resolved_camera_owner=(uintptr_t)camera;resolved_camera_cache_off=0x2da0;
    put64(controller+0x3f0,(uintptr_t)camera);atomic_store(&ev_local_pawn,(uintptr_t)camera);
    float xyz[]={100,200,300},angles[]={-30,45,2},cached=90,locked=138;
    memcpy(camera+0x2db0,xyz,sizeof(xyz));put32(camera+0x2dbc,0xffff);
    memcpy(camera+0x2dc0,angles,sizeof(angles));memcpy(camera+0x2dcc,&cached,4);memcpy(camera+0x38c,&locked,4);
    float render[6],roll=0;assert(render_camera_snapshot(render,&roll));
    assert(render[0]==1 && render[1]==2 && render[2]==3 && render[3]==45 && render[4]==-30 && render[5]==138 && roll==2);
    assert(getf(camera+0x2dcc)==90); // Effective FOV is read, caches remain owned by the engine.
    put64(controller+0x3f0,0);assert(!render_camera_snapshot(render,&roll));
    // A stale destination returns a failed checked write instead of SIGSEGV.
    void *none=mmap(NULL,4096,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(none!=MAP_FAILED);
    float value=120;assert(!ew_write((uintptr_t)none,&value,4));munmap(none,4096);
    puts("PASS: native FOV lock signature, cache preservation, ownership, restore/disable/recycling/world change, checked writes");
    self_memory_close(&ew_writer);self_memory_close(&ev_reader);
}
'''
with tempfile.TemporaryDirectory() as directory:
    src, binary = Path(directory) / "fov.c", Path(directory) / "fov"
    src.write_text(program)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-variable",
                    "-I", str(root / "internal/native"), "-I", str(root / "external/scanner"), str(src), "-lm", "-lpthread", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
