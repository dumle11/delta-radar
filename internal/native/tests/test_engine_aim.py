"""Exercise production aim selection and input ownership using a mock engine."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
visibility = (root / "internal/native/engine_visibility.h").read_text()
helpers = visibility[visibility.index("#define EV_MAX "):visibility.index("/* Worker-only lookup.")]
program = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include "self-memory.h"
#include "aim_math.h"
#define OFF_GUOBJECTARRAY_FROM_BASE 0x206aef48
#define OFF_CHARACTER_LAST_IS_DEAD 0xe5d
static uint64_t ue4_base;
static _Atomic int lib_running_flag;
''' + helpers + r'''
static uint64_t read_ptr(uint64_t p) { return ev_ptr(p); }
static int32_t read_i32(uint64_t p) { int32_t v=0; ev_read(p,&v,4); return v; }
static void resolve_fname(int32_t n,char *out,size_t size) { (void)n; if(size)out[0]=0; }
#include "engine_aim.h"
static unsigned char object[9][0xf00], array[0x28], items[9][0x18];
static uint64_t chunks[1];
static int ignore_count, pushes, releases, rotations;
static float last_rotation[3];
static void put64(void *p,uint64_t value) { memcpy(p,&value,8); }
static void put32(void *p,uint32_t value) { memcpy(p,&value,4); }
static void dispatch(void *controller,void *function,void *params) {
    assert(controller==object[1]);
    if(function==object[6]) {
        if(*(uint8_t *)params) { ignore_count++; pushes++; }
        else { ignore_count--; releases++; }
        assert(ignore_count>=0 && ignore_count<=1);
    } else {
        assert(function==object[5]); rotations++;
        memcpy(last_rotation,params,sizeof(last_rotation));
    }
}
static void tick_frame(EVFrame *frame) { frame->at_ms=ev_now(); ea_frame(frame); ea_tick(); }
int main(void) {
    /* Projection radius is the acquisition radius, including zoom changes. */
    assert(fabsf(aim_radius_pixels(1920,90,15)-257.2312f)<0.01f);
    assert(aim_radius_pixels(1920,45,15)>aim_radius_pixels(1920,90,15));
    assert(aim_radius_pixels(1920,NAN,15)==0);
    float camera[3]={0,0,0}, point[3]={1000,0,0}, rotation[3];
    assert(aim_rotation(camera,point,rotation) && fabsf(rotation[1])<0.001f);
    point[1]=1000; assert(aim_rotation(camera,point,rotation) && fabsf(rotation[1]-45)<0.001f);
    point[0]=-1000;point[1]=0;
    assert(!aim_inside(aim_screen_distance(camera,0,0,point),15));
    float radius=tanf(15*0.017453292519943295f);
    assert(aim_inside(radius*radius,15));
    assert(!aim_inside(radius*radius+0.0001f,15));
    assert(!aim_inside(NAN,15));

    ue4_base=(uintptr_t)array-OFF_GUOBJECTARRAY_FROM_BASE;
    chunks[0]=(uintptr_t)items;
    put32(array+0x14,9);put64(array+0x20,(uintptr_t)chunks);
    for(int i=0;i<9;i++) {
        put64(object[i]+8,0x9990);put64(object[i]+0x1c,i+1);put32(object[i]+0x24,i);
        put64(items[i],(uintptr_t)object[i]);put32(items[i]+0x10,100+i);
    }
    put64(object[1]+0x3f0,(uintptr_t)object[2]);
    assert(ev_object((uintptr_t)object[5],&ea_rotate_function));
    assert(ev_object((uintptr_t)object[6],&ea_ignore_function));
    EVFrame frame={0}; frame.aim_config=1|(5<<1)|(15<<8)|(2<<16);frame.count=2;
    assert(ev_object((uintptr_t)object[0],&frame.world));
    assert(ev_object((uintptr_t)object[1],&frame.controller));
    assert(ev_object((uintptr_t)object[2],&frame.pawn));
    for(int i=0;i<2;i++) {
        assert(ev_object((uintptr_t)object[3+i],&frame.jobs[i].actor));
        frame.jobs[i].aim_eligible=frame.jobs[i].aim_clear=frame.jobs[i].visible=1;
        frame.jobs[i].aim_point[0]=1000;frame.jobs[i].aim_point[1]=i?200:100;
    }
    ev_original=dispatch;atomic_store(&ea_ready,1);atomic_store(&ev_ready,1);
    atomic_store(&lib_running_flag,1);atomic_store(&ea_config,frame.aim_config);
    tick_frame(&frame);
    assert(atomic_load(&ea_locked_actor)==(uintptr_t)object[3]);
    assert(ignore_count==1 && pushes==1 && rotations==1);
    ea_tick();ea_tick();assert(pushes==1 && rotations==3); /* no stacked block */
    frame.jobs[0].aim_point[1]=500;frame.jobs[1].aim_point[1]=0;
    tick_frame(&frame);
    assert(atomic_load(&ea_locked_actor)==(uintptr_t)object[3]); /* sticky outside acquisition FOV */
    assert(fabsf(last_rotation[1]-26.565f)<0.01f);
    frame.jobs[0].aim_clear=0;frame.jobs[1].aim_clear=0;
    tick_frame(&frame);assert(ignore_count==0 && releases==1 && !atomic_load(&ea_locked_actor));
    tick_frame(&frame);assert(releases==1); /* exactly one release */
    frame.jobs[0].aim_clear=1;frame.jobs[0].aim_point[1]=0;
    tick_frame(&frame);assert(ignore_count==1);
    object[3][OFF_CHARACTER_LAST_IS_DEAD]=1;ea_tick();
    assert(ignore_count==0 && !atomic_load(&ea_locked_actor)); /* immediate death */
    object[3][OFF_CHARACTER_LAST_IS_DEAD]=0;
    tick_frame(&frame);atomic_store(&ea_config,0);ea_tick();
    assert(ignore_count==0); /* disabling restores manual look */
    atomic_store(&ea_config,frame.aim_config);tick_frame(&frame);
    ea_sample_ms-=101;ea_tick();assert(ignore_count==0); /* stale source */
    tick_frame(&frame);put64(object[1]+0x3f0,0);ea_tick();assert(ignore_count==0);
    put64(object[1]+0x3f0,(uintptr_t)object[2]);
    tick_frame(&frame);put32(items[3]+0x10,500);ea_tick();assert(ignore_count==0);
    put32(items[3]+0x10,103);
    tick_frame(&frame);atomic_store(&lib_running_flag,0);ea_tick();assert(ignore_count==0);
    assert(pushes==releases);
    self_memory_close(&ev_reader);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    cfile = Path(directory) / "engine_aim.c"
    executable = Path(directory) / "engine_aim"
    cfile.write_text(program)
    subprocess.run(["cc", "-std=c11", "-O2", "-pthread", "-I", str(root / "external/scanner"),
                    "-I", str(root / "internal/native"), str(cfile), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: FOV/zoom/boundary, nearest eligible acquisition, sticky identity, aim rotation, balanced look blocking, cover/death/disable/expiry/possession/recycling/stop release")
