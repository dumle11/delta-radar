"""Exercise the production center-ray trigger with synthetic physics and objects."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / "internal/native/engine_visibility.h").read_text()
helpers = source[source.index("#define EV_MAX"):source.index("/* ProcessEvent RVA")] + source[source.index("typedef struct { uint64_t data;"):source.index("/* Bind the UGPPhysics")]
program = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include "self-memory.h"
#define OFF_GUOBJECTARRAY_FROM_BASE 0x206aef48
static uint64_t ue4_base;
''' + helpers + r'''
typedef struct { uint64_t actor_address; char id[16], kind[8];
    int dead, team, visibility_known, visible_from_camera; float health; } Entity;
static Entity entities[3];
static const char *self_entity_id = "self";
static _Atomic int ea_autoshoot_active;
static int dead_now;
static int ea_alive(uint64_t actor) { return actor && !dead_now; }
#define INTERNAL_ENGINE_FIRE_H
static _Atomic unsigned ef_status;
static int input(int,int);
static void ef_publish(int on,int fire,uint64_t pawn,uint64_t pc,uint64_t target) {
    (void)pawn;(void)pc;(void)target;
    atomic_store(&ef_status,input(on,fire));
    atomic_store(&ea_autoshoot_active,atomic_load(&ef_status)==2);
}
#include "engine_trigger.h"
static unsigned char objects[6][0x40], items[6][0x18], array[0x28];
static uint64_t chunks[1];
static int enabled, firing, input_state, hit_index=2, hit_serial=102, result=1, blocking=1;
static float end[3];
static int input(int on,int fire) { enabled=on; firing=fire; return input_state ? input_state : fire?2:on?1:0; }
static void put(void *p,const void *v,int n) { memcpy(p,v,n); }
static void put32(void *p,int v) { put(p,&v,4); }
static void put64(void *p,uint64_t v) { put(p,&v,8); }
static void trace(void *receiver,void *function,void *data) {
    assert(receiver==objects[0] && function==objects[1]);
    EVTraceParams *p=data;
    assert(p->world==0x123456 && p->channel==3 && p->complex==1);
    assert(p->ignore.count==1 && *(uint64_t *)(uintptr_t)p->ignore.data==(uintptr_t)objects[4]);
    assert(fabsf(p->start[0]-100)<0.01f && fabsf(p->start[2]-300)<0.01f);
    memcpy(end,p->end,sizeof(end));
    p->result=result; p->hit[0]=blocking;
    put32(p->hit+0x68,hit_index); put32(p->hit+0x6c,hit_serial);
}
static void frame(unsigned config,float yaw,float pitch) {
    et_frame(3,1,2,3,yaw,pitch,config,0x123456,(uintptr_t)objects[0],(uintptr_t)objects[4]);
}
int main(void) {
    ue4_base=(uintptr_t)array-OFF_GUOBJECTARRAY_FROM_BASE;
    chunks[0]=(uintptr_t)items;
    put32(array+0x14,6);put64(array+0x20,(uintptr_t)chunks);
    for(int i=0;i<6;i++) {
        put64(objects[i]+8,0x9990);put64(objects[i]+0x1c,i+1);put32(objects[i]+0x24,i);
        put64(items[i],(uintptr_t)objects[i]);put32(items[i]+0x10,42);put32(items[i]+0x14,100+i);
    }
    assert(ev_object((uintptr_t)objects[0],&ev_receiver));
    assert(ev_object((uintptr_t)objects[1],&ev_function));
    ev_pe_fn=trace;
    entities[0]=(Entity){.actor_address=(uintptr_t)objects[2],.id="enemy",.kind="player",
        .team=0,.health=100,.visibility_known=1,.visible_from_camera=1};
    entities[1]=entities[0]; entities[1].actor_address=(uintptr_t)objects[3]; entities[1].team=1;
    entities[2]=entities[0]; entities[2].actor_address=(uintptr_t)objects[4]; strcpy(entities[2].id,"self");
    unsigned config=1u<<24; // Aim disabled: trigger must remain functional.
    frame(config,0,0); assert(enabled && firing && atomic_load(&ea_autoshoot_active));
    assert(fabsf(end[0]-200100)<0.1f && fabsf(end[1]-200)<0.1f);
    frame(config,90,30); assert(fabsf(end[2]-100300)<0.1f && end[1]>173000);
    for(int i=0;i<10;i++) { frame(config,0,0); assert(firing); } // Sustained fire, no tap pulses.
    entities[0].dead=1; frame(config,0,0); assert(!firing);
    entities[0].dead=0; dead_now=1; frame(config,0,0); assert(!firing); dead_now=0;
    entities[0].health=0; frame(config,0,0); assert(!firing);
    entities[0].health=NAN; frame(config,0,0); assert(!firing); entities[0].health=100;
    entities[0].visible_from_camera=0; frame(config,0,0); assert(!firing);
    entities[0].visible_from_camera=1;entities[0].visibility_known=0;frame(config,0,0);assert(!firing);
    entities[0].visibility_known=1;
    hit_index=3;hit_serial=103;frame(config,0,0);assert(!firing); // Teammate in front.
    hit_index=4;hit_serial=104;frame(config,0,0);assert(!firing); // Local pawn.
    hit_index=5;hit_serial=105;frame(config,0,0);assert(!firing); // Wall, no entity.
    hit_index=2;hit_serial=102;
    result=0;frame(config,0,0);assert(!firing); // Empty crosshair, regardless of aim lock.
    result=255;frame(config,0,0);assert(!firing); // Failed query.
    result=1;blocking=0;frame(config,0,0);assert(!firing);blocking=1;
    hit_serial=101;frame(config,0,0);assert(!firing); // Recycled object.
    hit_serial=102;strcpy(entities[0].kind,"ai");frame(config,0,0);assert(firing); // Range mannequin.
    input_state=3;frame(config,0,0);assert(atomic_load(&et_status)==3 && !atomic_load(&ea_autoshoot_active));
    input_state=0;frame(0,0,0);assert(!enabled && !firing);
    frame(config,NAN,0);assert(!enabled && !firing);
    frame(config,0,0);assert(firing);et_release();assert(!enabled && !firing && !atomic_load(&et_hit_actor));
    puts("PASS: center-ray geometry, nearest hit, enemy-only, cover, death, stale identity, independent trigger, hold/release, backend failure");
    self_memory_close(&ev_reader);
}
'''
with tempfile.TemporaryDirectory() as directory:
    src = Path(directory) / "trigger.c"
    binary = Path(directory) / "trigger"
    src.write_text(program)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-variable",
                    "-I", str(root / "internal/native"), "-I", str(root / "external/scanner"),
                    str(src), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
