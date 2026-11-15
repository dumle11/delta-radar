"""Production internal fire binding, GameThread execution, identity and release."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / "internal/native/engine_visibility.h").read_text()
helpers = source[source.index("typedef struct { uint64_t data;"):source.index("/* Bind the UGPPhysics")]
program = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <sys/syscall.h>
#include "self-memory.h"
#define OFF_GUOBJECTARRAY_FROM_BASE 0x206aef48
#define OFF_UOBJECT_NAME 0x1c
static uint64_t ue4_base;
''' + helpers + r'''
static _Atomic unsigned ea_config;
static _Atomic int ea_autoshoot_active, lib_running_flag;
static unsigned char objects[12][0x4000], items[12][24], array[0x28];
static uint64_t chunks[1];
static int dead[12];
static int ea_alive(uint64_t p) {
    for(int i=0;i<12;i++)if(p==(uintptr_t)objects[i])return !dead[i];
    return 0;
}
static int32_t read_i32(uint64_t p) { int32_t n=0; ev_read(p,&n,4); return n; }
static void resolve_fname(int32_t name,char *out,size_t n) {
    snprintf(out,n,"%s",name==20?"GPCharacterBase":name==21?"StartFire":name==22?"StopFire":"Other");
}
static int is_in_ue4(uint64_t p) { (void)p;return 0; }
static struct { uint64_t start,end; char perms[4]; } regions[1];
static int num_regions;
static int test_tid=7;
static long fake_syscall(long number,...) { (void)number;return test_tid; }
#ifndef __NR_gettid
#define __NR_gettid 999
#endif
#define syscall fake_syscall
#include "engine_fire.h"
#undef syscall
static int starts, stops, originals, depth_test;
static uint64_t last_pawn;
static void put32(void *p,int v) { memcpy(p,&v,4); }
static void put64(void *p,uint64_t v) { memcpy(p,&v,8); }
static void original(void *o,void *f,void *p) {
    (void)o;(void)f;(void)p;originals++;
    if(depth_test) { depth_test=0; ef_process_event(o,f,p); }
}
static void dispatch(void *pawn,void *function,void *params) {
    assert(ef_depth==1); // Called only inside outermost GameThread callback.
    assert(test_tid==7 && *(int32_t *)params==0);
    if(function==objects[4])starts++;
    else { assert(function==objects[5]);stops++; }
    last_pawn=(uintptr_t)pawn;
}
static EFCommand command(int pawn,int target,int weapon) {
    EFCommand c={.enabled=1,.fire=1,.at=ef_now()};
    assert(ef_ref((uintptr_t)objects[pawn],&c.pawn));
    assert(ef_ref((uintptr_t)objects[1],&c.controller));
    assert(ef_ref((uintptr_t)objects[target],&c.target));
    assert(ef_ref((uintptr_t)objects[weapon],&c.weapon));
    assert(ef_ref((uintptr_t)objects[4],&c.start));
    assert(ef_ref((uintptr_t)objects[5],&c.stop));
    put64(objects[1]+0x3f0,(uintptr_t)objects[pawn]);
    put64(objects[pawn]+0x3b18,(uintptr_t)objects[8]);
    put64(objects[8]+0x218,(uintptr_t)objects[weapon]);
    return c;
}
static void tick(void) { ef_last_tick=0;ef_process_event(objects[1],0,0); }
int main(void) {
    ue4_base=(uintptr_t)array-OFF_GUOBJECTARRAY_FROM_BASE;
    chunks[0]=(uintptr_t)items;put32(array+0x14,12);put64(array+0x20,(uintptr_t)chunks);
    for(int i=0;i<12;i++) {
        put64(objects[i]+8,(uintptr_t)objects[6]);put32(objects[i]+0x1c,i+30);put32(objects[i]+0x24,i);
        put64(items[i],(uintptr_t)objects[i]);put32(items[i]+0x10,42);put32(items[i]+0x14,100+i);
    }
    // Exact SDK owner, argument bytes, Native flag, and ExecFunction at +D8.
    put32(objects[6]+0x1c,20);put64(objects[6]+0x50,(uintptr_t)objects[4]);
    put32(objects[4]+0x1c,21);put32(objects[5]+0x1c,22);
    put64(objects[4]+0x10,(uintptr_t)objects[6]);put64(objects[5]+0x10,(uintptr_t)objects[6]);
    put64(objects[4]+0x28,(uintptr_t)objects[5]);
    uint16_t bytes=4;memcpy(objects[4]+0xb2,&bytes,2);memcpy(objects[5]+0xb2,&bytes,2);
    put32(objects[4]+0xb8,0x400);put32(objects[5]+0xb8,0x400);
    put64(objects[4]+0xd8,ue4_base+0xc595a6c);put64(objects[5]+0xd8,ue4_base+0xc595c18);
    put32(items[4]+0x14,0);put32(items[5]+0x14,0); // Native functions have no allocated weak serial.
    assert(ef_bind((uintptr_t)objects[0]));
    memset(&ef_start,0,sizeof(ef_start));put32(objects[4]+0xb8,0);
    assert(!ef_bind((uintptr_t)objects[0]));put32(objects[4]+0xb8,0x400);
    assert(ef_bind((uintptr_t)objects[0]));
    assert(!ef_hook((uintptr_t)objects[0])); // Unverified image must not be patched.
    ev_pe_fn=dispatch;ef_original=original;atomic_store(&ef_game_tid,7);
    atomic_store(&ea_config,1u<<24);atomic_store(&lib_running_flag,1);
    ef_request=command(0,2,7);
    test_tid=8;tick();assert(starts==0); // Scanner/other threads cannot call gameplay.
    test_tid=7;depth_test=1;tick();assert(starts==1 && originals==3);
    for(int i=0;i<10;i++)tick();assert(starts==1 && stops==0 && atomic_load(&ea_autoshoot_active));
    put32(items[4]+0x14,333);put32(items[5]+0x14,334); // First weak serial allocation preserves rooted metadata.
    ef_request.fire=0;tick();assert(stops==1 && !atomic_load(&ea_autoshoot_active));
    tick();assert(stops==1); // Balanced stop exactly once.
    ef_request=command(0,2,7);tick();dead[2]=1;tick();assert(starts==2 && stops==2);dead[2]=0;
    ef_request=command(0,2,7);tick();ef_request.at-=101;tick();assert(stops==3); // Expired observation.
    ef_request=command(0,2,7);tick();atomic_store(&ea_config,0);tick();assert(stops==4);
    atomic_store(&ea_config,1u<<24);ef_request=command(0,2,7);tick();
    ef_request=command(0,3,7);tick();assert(stops==5 && starts==6); // New target gets a fresh start.
    ef_request=command(0,3,9);tick();assert(stops==6 && starts==7); // Weapon switch, same pawn.
    ef_request=command(10,3,9);tick();assert(stops==7 && starts==8 && last_pawn==(uintptr_t)objects[10]);
    put32(items[3]+0x14,1000);tick();assert(stops==8); // Recycled target rejected.
    ef_request=command(10,2,9);tick();put32(items[10]+0x14,1000);tick();
    assert(stops==8 && !atomic_load(&ea_autoshoot_active)); // Never call a recycled old pawn.
    ef_request=command(0,2,7);tick();atomic_store(&lib_running_flag,0);tick();assert(stops==9);
    puts("PASS: SDK method binding, GameThread-only dispatch, reentrancy, sustained start, balanced stop, death/expiry/disable, target/weapon/possession switch, recycling, scanner stop");
    self_memory_close(&ev_reader);
}
'''
with tempfile.TemporaryDirectory() as directory:
    src, binary = Path(directory) / "fire.c", Path(directory) / "fire"
    src.write_text(program)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-variable",
                    "-I", str(root / "internal/native"), "-I", str(root / "external/scanner"),
                    str(src), "-lm", "-lpthread", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
