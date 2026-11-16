"""Run production stamina handling against synthetic SDK objects and checked writes."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
visibility = (root / "internal/native/engine_visibility.h").read_text()
helpers = visibility[visibility.index("typedef struct { uint64_t data;"):visibility.index("/* Bind the UGPPhysics")]
weapon = (root / "internal/native/engine_weapon.h").read_text()
writes = weapon[weapon.index("/* Kernel-checked destination copy"):weapon.index("static int ew_owned_by")]
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
#define OFF_UOBJECT_NAME 0x1c
#define OFF_CHARACTER_LAST_IS_DEAD 0xe5d
static uint64_t ue4_base;
''' + helpers + r'''
static unsigned char objects[6][0x5000], classes[3][0x80], items[6][24], array[0x28];
static uint64_t chunks[1];
static __thread SelfMemoryReader ew_writer={-1,-1};
static int is_any_ptr(uint64_t p) { return p>=65536 && p<0xffffffffffffULL && !(p&3); }
static int32_t read_i32(uint64_t p) { int32_t n=0;ev_read(p,&n,4);return n; }
static void resolve_fname(int32_t name,char *out,size_t n) {
    snprintf(out,n,"%s",name==10?"DFMCharacter":name==11?"GPStaminaDataComponent":name==12?"GPAttributeSetSpeed":"Other");
}
static void _diag(const char *text) { (void)text; }
''' + writes + r'''
#include "engine_stamina.h"
static void put32(void *p,int n) { memcpy(p,&n,4); }
static void put64(void *p,uint64_t n) { memcpy(p,&n,8); }
static void putf(void *p,float n) { memcpy(p,&n,4); }
static float getf(void *p) { float n;memcpy(&n,p,4);return n; }
static void bind(int pawn,int component,int speed) {
    put64(objects[pawn]+0x3ad0,(uintptr_t)objects[component]);
    put64(objects[component]+0x1d8,(uintptr_t)objects[pawn]);
    put64(objects[component]+0x1e0,(uintptr_t)objects[speed]);
    putf(objects[speed]+0x124,75);putf(objects[speed]+0x134,100);
    for(int i=0;i<4;i++)putf(objects[speed]+es_offsets[i],i+1);
}
int main(void) {
    ue4_base=(uintptr_t)array-OFF_GUOBJECTARRAY_FROM_BASE;
    chunks[0]=(uintptr_t)items;put32(array+0x14,6);put64(array+0x20,(uintptr_t)chunks);
    for(int i=0;i<3;i++) {put32(classes[i]+0x1c,10+i);put32(classes[i]+0x3c,i==0?0x5000:i==1?0x208:0x400);}
    for(int i=0;i<6;i++) {
        put64(objects[i]+8,(uintptr_t)classes[i%3]);put32(objects[i]+0x1c,30+i);put32(objects[i]+0x24,i);
        put64(items[i],(uintptr_t)objects[i]);put32(items[i]+0x14,100+i);
    }
    bind(0,1,2);bind(3,4,5);
    unsigned char before[0x5000];memcpy(before,objects[2],sizeof(before));
    es_update((uintptr_t)objects[0]);assert(!memcmp(before,objects[2],sizeof(before))); // Default disabled.
    atomic_store(&es_enabled,1);es_update((uintptr_t)objects[0]);assert(atomic_load(&es_state)==2 && es_saved==15);
    for(int i=0;i<4;i++)assert(getf(objects[2]+es_offsets[i])==0);
    for(unsigned i=0;i<sizeof(before);i++) {
        int scalar=0;for(int j=0;j<4;j++)if(i>=es_offsets[j] && i<es_offsets[j]+4)scalar=1;
        if(!scalar)assert(before[i]==objects[2][i]); // Actual stamina, max, metadata and other attributes preserved.
    }
    es_update((uintptr_t)objects[0]); // Repeated update never saves the applied zero as baseline.
    atomic_store(&es_enabled,0);es_update((uintptr_t)objects[0]);assert(!memcmp(before,objects[2],sizeof(before)));
    atomic_store(&es_enabled,1);es_update((uintptr_t)objects[0]);putf(objects[2]+0x154,8);
    es_update((uintptr_t)objects[3]); // Possession switch restores old object without clobbering a host rewrite.
    assert(getf(objects[2]+0x150)==1 && getf(objects[2]+0x154)==8 && getf(objects[2]+0x1c0)==3);
    assert(getf(objects[5]+0x150)==0);
    objects[3][OFF_CHARACTER_LAST_IS_DEAD]=1;es_update((uintptr_t)objects[3]);
    assert(getf(objects[5]+0x150)==1 && !es_saved);objects[3][OFF_CHARACTER_LAST_IS_DEAD]=0;
    es_update((uintptr_t)objects[3]);put32(items[5]+0x14,999);es_release();
    assert(getf(objects[5]+0x150)==0); // Never restore into a recycled object.
    bind(3,4,5);put64(objects[4]+0x1d8,(uintptr_t)objects[0]);es_update((uintptr_t)objects[3]);
    assert(atomic_load(&es_state)==3 && getf(objects[5]+0x150)==1); // Owner mismatch.
    bind(3,4,5);putf(objects[5]+0x134,NAN);es_update((uintptr_t)objects[3]);assert(!es_saved);
    bind(3,4,5);es_update((uintptr_t)objects[3]);es_update(0);assert(!es_saved && getf(objects[5]+0x150)==1);
    es_update((uintptr_t)objects[3]);es_release();assert(getf(objects[5]+0x150)==1); // Scanner cleanup.
    puts("PASS: stamina SDK ownership, scalar-only writes, default off, repeated updates, restore, host rewrites, possession/death/recycling, invalid values and missing pawn");
    self_memory_close(&ew_writer);self_memory_close(&ev_reader);
}
'''
with tempfile.TemporaryDirectory() as directory:
    src, binary = Path(directory) / "stamina.c", Path(directory) / "stamina"
    src.write_text(program)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-function", "-Wno-unused-variable",
                    "-I", str(root / "internal/native"), "-I", str(root / "external/scanner"),
                    str(src), "-lm", "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
