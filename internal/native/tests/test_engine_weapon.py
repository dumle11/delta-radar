"""Exercise production weapon overrides on synthetic UObject allocations."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / "internal/native/engine_visibility.h").read_text()
helpers = source[source.index("typedef struct { uint64_t data;"):source.index("/* Bind the UGPPhysics")]
helpers += source[source.index("__attribute__((noinline))"):source.index("static int ev_bind_class(")]
# Helpers contain the read/object-identity layer and string decoder, not engine dispatch.
program = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/mman.h>
#include "self-memory.h"
#define OFF_GUOBJECTARRAY_FROM_BASE 0x206aef48
#define OFF_UOBJECT_NAME 0x1c
#define OFF_UOBJECT_OUTER 0x10
#define OFF_CHARACTER_LAST_IS_DEAD 0xe5d
static uint64_t ue4_base;
''' + helpers + r'''
static unsigned char objects[17][0x6000], classes[17][0x100], array[0x28], items[17][0x18];
static uint64_t chunks[1], weapons[2], modifiers[3];
static struct { int32_t index,serial; } weak[19];
static struct { int32_t index,serial; } weak_data[1];
static int is_any_ptr(uint64_t p) { return p>65536 && p<0xffffffffffffULL && !(p&3); }
static int32_t read_i32(uint64_t p) { int32_t v=0; ev_read(p,&v,4); return v; }
static void resolve_fname(int32_t i,char *out,size_t n) {
    const char *names[]={"?","Pawn","WeaponManagerComponent","WeaponBase",
        "WeaponFuncComponentFireShake","WeaponFuncComponentSpread","WeaponFireRecoilDFM","WeaponFireSpreadDFM",
        "GPWeaponDataComponentFiring","WeaponFireRecoilPUBGM","WeaponFuncComponentGunKick","WeaponFuncComponentGunSway","Controller","PlayerCameraManager","GPCameraModifier_WeaponRecoil","GPCameraModifier_WeaponGunSway","GPCameraModifier_Fire"};
    snprintf(out,n,"%s",i>=0 && i<17 ? names[i] : "?");
}
static void _diag(const char *s) { (void)s; }
#include "engine_weapon.h"
static void put64(void *p,uint64_t value) { memcpy(p,&value,8); }
static void put32(void *p,uint32_t value) { memcpy(p,&value,4); }
static void putfloat(void *p,float value) { memcpy(p,&value,4); }
static float getfloat(void *p) { float v;memcpy(&v,p,4);return v; }
static void object_init(int i,int cls,int size) {
    put64(objects[i]+8,(uintptr_t)classes[cls]);put64(objects[i]+0x1c,i+1000);
    put32(objects[i]+0x24,i);put32(objects[i]+0x18,0);
    put64(items[i],(uintptr_t)objects[i]);put32(items[i]+0x10,100+i);
    put32(classes[cls]+0x1c,cls);put32(classes[cls]+0x3c,size);
}
int main(void) {
    ue4_base=(uintptr_t)array-OFF_GUOBJECTARRAY_FROM_BASE;
    chunks[0]=(uintptr_t)items;put32(array+0x14,17);put64(array+0x20,(uintptr_t)chunks);
    object_init(0,1,0x4000);object_init(1,2,0x700);object_init(2,3,0x1800);
    object_init(3,4,0x9c0);object_init(4,5,0x518);objects[4][0x1e0]=1;
    for(int i=0x288;i<0x9c0;i+=4) putfloat(objects[3]+i,3.25f);
    /* Distinct numeric/opaque data lets us detect any accidental broad erase. */
    for(int i=0x28;i<0x1db0;i+=4) putfloat(objects[5]+i,1.25f);
    for(int i=0x28;i<0x7d8;i+=4) putfloat(objects[6]+i,2.5f);
    object_init(5,6,0x1db0);object_init(6,7,0x7d8);object_init(7,3,0x1800);
    object_init(12,12,0x700);object_init(13,13,0x5000);
    put64(objects[0]+0x3a8,(uintptr_t)objects[12]);put64(objects[12]+0x408,(uintptr_t)objects[13]);
    put64(objects[13]+0x370,(uintptr_t)objects[12]);
    for(int k=0;k<3;k++) {object_init(14+k,14+k,k==2?0x50:0x48);put64(objects[14+k]+0x30,(uintptr_t)objects[13]);
        objects[14+k][k==2?0x48:0x44]=1;modifiers[k]=(uintptr_t)objects[14+k];}
    EVArray mods={(uintptr_t)modifiers,3,3};memcpy(objects[13]+0x4260,&mods,sizeof(mods));
    object_init(11,11,0x1690);put64(objects[11]+0x10,(uintptr_t)objects[2]);
    putfloat(objects[11]+0x1cc,1);putfloat(objects[11]+0x260,1);putfloat(objects[11]+0xf80,1);putfloat(objects[11]+0x1350,1);
    object_init(10,10,0x360);put64(objects[10]+0x10,(uintptr_t)objects[2]);
    putfloat(objects[10]+0x25c,1);putfloat(objects[10]+0x260,0.75f);
    object_init(8,8,0xa08);object_init(9,9,0xb8);
    put64(objects[0]+0x3b18,(uintptr_t)objects[1]);put64(objects[1]+0x148,(uintptr_t)objects[0]);
    weapons[0]=(uintptr_t)objects[2];weapons[1]=(uintptr_t)objects[7];
    EVArray list={(uintptr_t)weapons,2,2};memcpy(objects[1]+0x198,&list,sizeof(list));
    put64(objects[1]+WM_OFF_CURRENT_ACTIVE,(uintptr_t)objects[2]);
    put64(objects[2]+0xa60,(uintptr_t)objects[0]);put64(objects[7]+0xa60,(uintptr_t)objects[0]);
    for(int i=0;i<19;i++) weak[i].index=-1;
    weak[2].index=11;weak[2].serial=111;
    weak[1].index=10;weak[1].serial=110;
    weak[0].index=3;weak[0].serial=103;weak[18].index=4;weak[18].serial=104;
    list=(EVArray){(uintptr_t)weak,19,19};memcpy(objects[2]+0xb68,&list,sizeof(list));
    put64(objects[3]+0x10,(uintptr_t)objects[2]);put64(objects[4]+0x10,(uintptr_t)objects[2]);
    put64(objects[3]+0x968,(uintptr_t)objects[5]);put64(objects[4]+0x1f8,(uintptr_t)objects[6]);
    objects[3][0x288+0x30+0xc]=0x81;
    assert(ew_bind_serial((uintptr_t)objects[2]) && ew_serial_offset==0x10);
    assert(ew_resolve_weak((uintptr_t)weak,18)==(uintptr_t)objects[4]);
    assert(ew_find_component((uintptr_t)objects[2],"WeaponFuncComponentSpread",0x518)==(uintptr_t)objects[4]);
    assert(ew_get_current_weapon((uintptr_t)objects[0])==(uintptr_t)objects[2]);
    unsigned char recoil_before[0x1db0],spread_before[0x7d8],shake_before[0x9c0];
    memcpy(recoil_before,objects[5],sizeof(recoil_before));memcpy(spread_before,objects[6],sizeof(spread_before));
    memcpy(shake_before,objects[3],sizeof(shake_before));
    atomic_store(&ew_no_recoil,1);atomic_store(&ew_no_spread,1);
    ew_update((uintptr_t)objects[0]);
    assert(!objects[14][0x44] && !objects[15][0x44] && !objects[16][0x48]);
    assert(ew_spread_gate_group.count==1 && !objects[4][0x1e0]);
    assert(ew_shake_group.count==73 && getfloat(objects[3]+0x288+0x49c)==0);
    assert(getfloat(objects[3]+0x288+0x468)==3.25f); /* timing preserved */
    assert(getfloat(objects[3]+0x288+0x30+0x28)==3.25f); /* curve storage preserved */
    assert(ew_sway_group.count==4 && getfloat(objects[11]+0xf80)==0 && getfloat(objects[11]+0x1350)==0);
    assert(ew_kick_group.count==2 && getfloat(objects[10]+0x25c)==0 && getfloat(objects[10]+0x260)==0);
    assert(getfloat(objects[6]+0x37c)==0 && getfloat(objects[6]+0x380)==0 && getfloat(objects[6]+0x3bc)==0 && getfloat(objects[6]+0x3c0)==0);
    assert(getfloat(objects[6]+0x350+0x18)==2.5f); /* custom distribution storage preserved */
    assert(getfloat(objects[6]+0x3fc)==0 && getfloat(objects[6]+0x404)==EW_SPREAD_CAP && getfloat(objects[6]+0x5c4)==0 && getfloat(objects[6]+0x5cc)==EW_SPREAD_CAP);
    assert(getfloat(objects[6]+0x3d0)==0 && getfloat(objects[6]+0x598)==0 && getfloat(objects[6]+0x760)==0);
    assert(atomic_load(&ew_recoil_zeros)==1 && atomic_load(&ew_spread_zeros)==1);
    for(unsigned c=0;c<2;c++) for(unsigned pose=0;pose<6;pose++) {
        float factor=1; memcpy(&factor,objects[5]+(c?0xd48:0x88)+0xa28+pose*4,4);
        assert(factor==EW_RECOIL_CANCEL); /* additive cancellation of cached patterns */
    }
    assert(getfloat(objects[5]+0x88+0xa8)==0 && getfloat(objects[5]+0xd48+0x6c8+0xa8)==0);
    assert(!memcmp(objects[5]+0x1aa8,recoil_before+0x1aa8,0x140)); /* runtime/curve pointers preserved */
    assert(getfloat(objects[6]+0x50+4)==0 && getfloat(objects[6]+0x1d0+0x14)==EW_SPREAD_CAP);
    assert(!memcmp(objects[6]+0x50+0x70,spread_before+0x50+0x70,0x110)); /* pose map untouched */
    assert(!memcmp(objects[6]+0x1d0+0x70,spread_before+0x1d0+0x70,0x110));
    assert(objects[3][0x288+0x30+0xc]==0x80); /* unrelated flag bit preserved */
    atomic_store(&ew_no_recoil,0);atomic_store(&ew_no_spread,0);ew_update((uintptr_t)objects[0]);
    assert(!memcmp(objects[5],recoil_before,sizeof(recoil_before)));
    assert(objects[14][0x44]==1 && objects[15][0x44]==1 && objects[16][0x48]==1);
    assert(getfloat(objects[11]+0xf80)==1 && getfloat(objects[11]+0x1350)==1);
    assert(getfloat(objects[10]+0x25c)==1 && getfloat(objects[10]+0x260)==0.75f);
    assert(!memcmp(objects[6],spread_before,sizeof(spread_before)));
    assert(objects[4][0x1e0]==1);
    assert(!memcmp(objects[3],shake_before,sizeof(shake_before)));
    /* No Weapon Sway works alone; unrelated weapon/camera state is retained. */
    unsigned char sway_before[0x1690];memcpy(sway_before,objects[11],sizeof(sway_before));
    atomic_store(&ew_no_sway,1);ew_update((uintptr_t)objects[0]);
    assert(ew_sway_group.count==4 && getfloat(objects[11]+0x1cc)==0 && getfloat(objects[11]+0x260)==0);
    assert(objects[14][0x44]==1 && !objects[15][0x44] && objects[16][0x48]==1);
    assert(!memcmp(objects[5],recoil_before,sizeof(recoil_before)) && !memcmp(objects[6],spread_before,sizeof(spread_before)) && !memcmp(objects[3],shake_before,sizeof(shake_before)));
    for(unsigned i=0;i<sizeof(sway_before);i++) {
        int scalar=(i>=0x1cc&&i<0x1d0)||(i>=0x260&&i<0x264)||(i>=0xf80&&i<0xf84)||(i>=0x1350&&i<0x1354);
        if(!scalar) assert(objects[11][i]==sway_before[i]);
    }
    atomic_store(&ew_no_recoil,1);ew_update((uintptr_t)objects[0]);
    atomic_store(&ew_no_recoil,0);ew_update((uintptr_t)objects[0]);
    assert(getfloat(objects[11]+0xf80)==0 && !objects[15][0x44]);
    assert(objects[14][0x44]==1 && objects[16][0x48]==1);
    putfloat(objects[11]+0x260,0.6f);ew_update((uintptr_t)objects[0]);
    atomic_store(&ew_no_sway,0);ew_update((uintptr_t)objects[0]);
    assert(getfloat(objects[11]+0x260)==0.6f && getfloat(objects[11]+0xf80)==1 && objects[15][0x44]==1);
    putfloat(objects[11]+0x260,1);assert(!memcmp(objects[11],sway_before,sizeof(sway_before)));
    atomic_store(&ew_no_sway,1);ew_update((uintptr_t)objects[0]);
    put64(objects[1]+WM_OFF_CURRENT_ACTIVE,(uintptr_t)objects[7]);ew_update((uintptr_t)objects[0]);
    assert(!ew_sway_group.count && getfloat(objects[11]+0xf80)==1);
    atomic_store(&ew_no_sway,0);ew_update((uintptr_t)objects[0]);assert(objects[15][0x44]==1);
    put64(objects[1]+WM_OFF_CURRENT_ACTIVE,(uintptr_t)objects[2]);
    /* A second strategy has no FireShake DFM cache. Its firing-data method
     * must be followed, with its own layout rather than DFM offsets. */
    for(int i=0x48;i<0xb8;i+=4) putfloat(objects[9]+i,1.75f);
    put64(objects[3]+0x968,0);put64(objects[3]+0x960,(uintptr_t)objects[8]);
    put64(objects[8]+0x10,(uintptr_t)objects[2]);put64(objects[8]+0x578,(uintptr_t)objects[9]);
    atomic_store(&ew_no_recoil,1);ew_update((uintptr_t)objects[0]);
    assert(ew_recoil_group.owner.address==(uintptr_t)objects[9] && ew_recoil_group.count==22);
    assert(getfloat(objects[9]+0x48)==1.75f && getfloat(objects[9]+0x80)==1.75f); /* preserve timing */
    assert(getfloat(objects[9]+0x4c)==0 && getfloat(objects[9]+0x80+0x2c)==0);
    atomic_store(&ew_no_recoil,0);ew_update((uintptr_t)objects[0]);
    assert(getfloat(objects[9]+0x4c)==1.75f && getfloat(objects[9]+0x80+0x2c)==1.75f);
    /* A missing FireShake firing cache must resolve the weapon's typed data
     * list; a transient Outer uses the explicit owner interface instead. */
    weak_data[0].index=8;weak_data[0].serial=108;
    list=(EVArray){(uintptr_t)weak_data,1,1};memcpy(objects[2]+0xac8,&list,sizeof(list));
    put64(objects[3]+0x960,0);put64(objects[8]+0x10,0);
    put64(objects[8]+0x140,(uintptr_t)objects[2]);put64(objects[8]+0x138,(uintptr_t)objects[0]);
    atomic_store(&ew_no_recoil,1);ew_update((uintptr_t)objects[0]);
    assert(ew_recoil_group.owner.address==(uintptr_t)objects[9] && getfloat(objects[9]+0x4c)==0);
    atomic_store(&ew_no_recoil,0);ew_update((uintptr_t)objects[0]);
    assert(getfloat(objects[9]+0x4c)==1.75f);
    put64(objects[8]+0x578,0);
    put64(objects[3]+0x968,(uintptr_t)objects[5]);
    atomic_store(&ew_no_recoil,1);atomic_store(&ew_no_spread,1);ew_update((uintptr_t)objects[0]);
    putfloat(objects[6]+0x50+4,3.5f);ew_update((uintptr_t)objects[0]);
    atomic_store(&ew_no_spread,0);ew_update((uintptr_t)objects[0]);
    assert(getfloat(objects[6]+0x50+4)==3.5f); /* host reconfiguration restored */
    atomic_store(&ew_no_spread,1);weak[18].serial++;ew_update((uintptr_t)objects[0]);
    assert(ew_spread_group.count==0);weak[18].serial--;
    ew_update((uintptr_t)objects[0]);assert(ew_spread_group.count>0);
    put64(objects[1]+0x1d0,(uintptr_t)objects[2]);ew_update((uintptr_t)objects[0]);
    assert(!ew_recoil_group.count && !ew_spread_group.count); /* empty slot: never modify holstered weapon */
    put64(objects[1]+0x1d0,0);ew_update((uintptr_t)objects[0]);
    put32(items[6]+0x10,500);putfloat(objects[6]+0x50+4,9.0f);
    ew_release();assert(getfloat(objects[6]+0x50+4)==9.0f); /* recycled allocation not restored */
    /* A shuffled FUObjectItem tail is recovered from actual weak references,
     * without changing the general reader's legacy identity assumptions. */
    for (int i=0;i<17;i++) { put32(items[i]+0xc,100+i);put32(items[i]+0x10,0); }
    ew_serial_offset=-1;
    assert(ew_bind_serial((uintptr_t)objects[2]) && ew_serial_offset==0xc);
    assert(ew_resolve_weak((uintptr_t)weak,18)==(uintptr_t)objects[4]);
    weak[18].serial++;
    assert(!ew_resolve_weak((uintptr_t)weak,18));weak[18].serial--;
    for (int i=0;i<17;i++) put32(items[i]+0x14,100+i);
    ew_serial_offset=-1;assert(!ew_bind_serial((uintptr_t)objects[2])); /* ambiguity fails closed */
    for (int i=0;i<17;i++) put32(items[i]+0xc,0);
    assert(ew_bind_serial((uintptr_t)objects[2]) && ew_serial_offset==0x14);
    assert(ew_resolve_weak((uintptr_t)weak,18)==(uintptr_t)objects[4]);
    float value=4,zero=0;
    assert(!ew_write(1,&zero,4));assert(ew_write((uintptr_t)&value,&zero,4) && value==0);
    long size=sysconf(_SC_PAGESIZE);
    void *page=mmap(NULL,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANON,-1,0);assert(page!=MAP_FAILED);
    *(float *)page=7;mprotect(page,size,PROT_READ);
    assert(!ew_write((uintptr_t)page,&zero,4) && *(float *)page==7);
    munmap(page,size);value=5;assert(ew_write((uintptr_t)&value,&zero,4) && value==0);
    self_memory_close(&ew_writer);self_memory_close(&ev_reader);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    cfile = Path(directory) / "weapon.c"
    executable = Path(directory) / "weapon"
    cfile.write_text(program)
    subprocess.run(["cc", "-std=c11", "-O2", "-I", str(root / "external/scanner"),
                    "-I", str(root / "internal/native"), str(cfile), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: 19-entry weak list, identity/ownership, correct spread/recoil fields, opaque-data preservation, standalone sway and recoil interaction, host rewrite/toggle/switch restoration, stale references, checked-write recovery")
