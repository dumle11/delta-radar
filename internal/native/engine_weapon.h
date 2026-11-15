#ifndef ENGINE_WEAPON_H
#define ENGINE_WEAPON_H
/* Matching SDK, per-instance scalar overrides. Never erase opaque runtime
 * structs, pointers or containers. Restore changes on toggle/weapon change. */
#define WM_OFF_FROM_PAWN 0x3b18
#define WM_OFF_WEAPON_LIST 0x198
#define WM_OFF_WEAPON_SLOT 0x1c8
#define WM_OFF_CURRENT_ACTIVE (0x1e8 + 0x30)
#define WB_OFF_FIRESHAKE_CACHE 0xbd0
#define WB_OFF_FUNC_LIST 0xb68
#define FS_OFF_FIRE_RECOIL 0x968
#define FC_SPREAD_OFF_METHOD 0x1f8
#define EW_MAX_FIELDS 96
static _Atomic int ew_no_recoil, ew_no_spread;
static _Atomic unsigned ew_recoil_zeros, ew_spread_zeros;
static unsigned ew_diag_seq;
static int ew_serial_offset = -1;
static uint64_t ew_shake_seen, ew_recoil_seen, ew_firing_seen;
static uint32_t ew_bad_field;
static unsigned ew_host_rewrites;
static __thread SelfMemoryReader ew_writer = {-1,-1};
typedef struct { uint32_t offset; uint8_t size, mask; unsigned char before[4], applied[4]; } EWField;
typedef struct { EVObject owner, source; uint32_t source_link; int count; EWField fields[EW_MAX_FIELDS]; } EWGroup;
static EWGroup ew_recoil_group, ew_shake_group, ew_kick_group, ew_spread_group;

/* Kernel-checked destination copy instead of dereferencing a retired object. */
static int ew_write(uint64_t address, const void *data, size_t size) {
    if (!data || !size || size>4 || address<0x10000 || address>UINTPTR_MAX-size) return 0;
    if (ew_writer.read_fd<0 && self_memory_open(&ew_writer)<0) return 0;
    ssize_t n;
    do { n=write(ew_writer.write_fd,data,size); } while(n<0 && errno==EINTR);
    if (n!=(ssize_t)size) { self_memory_drain(&ew_writer); return 0; }
    do { n=read(ew_writer.read_fd,(void *)(uintptr_t)address,size); } while(n<0 && errno==EINTR);
    if (n!=(ssize_t)size) { self_memory_drain(&ew_writer); return 0; }
    return 1;
}
static int ew_is_a(uint64_t object, const char *wanted, uint32_t minimum_size) {
    EVObject ref; uint32_t flags=0; int32_t size=0;
    if (!ev_object(object,&ref) || !ev_read(object+0x18,&flags,4) || (flags&0x10) ||
        !ev_read(ref.cls+0x3c,&size,4) || size<(int32_t)minimum_size) return 0;
    for (uint64_t cls=ref.cls,n=0; is_any_ptr(cls) && n<32; n++,cls=ev_ptr(cls+0x40)) {
        char name[96]; resolve_fname(read_i32(cls+OFF_UOBJECT_NAME),name,sizeof(name));
        if (!strcmp(name,wanted)) return 1;
    }
    return 0;
}
static int ew_owned_by(uint64_t component, uint64_t weapon) {
    /* Runtime components can live under a transient Outer while retaining
     * their explicit GPWeaponComponentImplement owner interface. */
    if (ev_ptr(component+0x140)==weapon && ev_ptr(component+0x138)==ev_ptr(weapon+0xa60)) return 1;
    for (uint64_t outer=ev_ptr(component+OFF_UOBJECT_OUTER),n=0; outer && n<4; n++,outer=ev_ptr(outer+OFF_UOBJECT_OUTER))
        if (outer==weapon) return 1;
    return 0;
}
/* The SDK leaves FUObjectItem's tail opaque. Infer its serial field only from
 * at least two independent, owned weak references with matching registered
 * UObject indices. Ambiguous layouts stay unsupported; nothing is written. */
static int ew_bind_serial(uint64_t weapon) {
    if (ew_serial_offset>=0) return 1;
    EVArray list={0};
    if (!ev_read(weapon+WB_OFF_FUNC_LIST,&list,sizeof(list)) || !is_any_ptr(list.data) ||
        list.count<2 || list.count>128 || list.capacity<list.count || list.capacity>4096) return 0;
    int scores[4]={0}, seen[128], seen_count=0;
    for (int i=0;i<list.count;i++) {
        struct { int32_t index,serial; } weak;
        unsigned char item_bytes[24]; uint64_t item,object; EVObject ref;
        if (!ev_read(list.data+(uint64_t)i*8,&weak,sizeof(weak)) || weak.serial<=0 ||
            !ev_item(weak.index,&item) || !ev_read(item,item_bytes,sizeof(item_bytes))) continue;
        int duplicate=0; for (int j=0;j<seen_count;j++) if (seen[j]==weak.index) duplicate=1;
        if (duplicate) continue;
        memcpy(&object,item_bytes,8);
        if (!ev_object(object,&ref) || ref.index!=weak.index || !ew_owned_by(object,weapon)) continue;
        seen[seen_count++]=weak.index;
        for (int j=0;j<4;j++) {
            int32_t value; memcpy(&value,item_bytes+8+j*4,4);
            if (value==weak.serial) scores[j]++;
        }
    }
    int chosen=-1;
    for (int j=0;j<4;j++) if (scores[j]>=2) {
        if (chosen>=0) return 0;
        chosen=j;
    }
    if (chosen<0) return 0;
    ew_serial_offset=8+chosen*4;
    return 1;
}
static int ew_object(uint64_t address, EVObject *out) {
    uint64_t item;
    return ew_serial_offset>=0 && ev_object(address,out) && ev_item(out->index,&item) &&
        ev_read(item+(uint64_t)ew_serial_offset,&out->serial,4);
}
static int ew_same(const EVObject *ref) {
    EVObject now;
    return ref->address && ew_object(ref->address,&now) && now.cls==ref->cls &&
        now.name==ref->name && now.index==ref->index && now.serial==ref->serial;
}
static uint64_t ew_resolve_weak(uint64_t data, int index) {
    struct { int32_t index,serial; } weak;
    unsigned char bytes[0x18]; uint64_t item;
    if (index<0 || !ev_read(data+(uint64_t)index*8,&weak,sizeof(weak)) || weak.serial<=0 ||
        !ev_item(weak.index,&item) || !ev_read(item,bytes,sizeof(bytes))) return 0;
    int32_t serial; uint64_t object;
    if (ew_serial_offset<0) return 0;
    memcpy(&serial,bytes+ew_serial_offset,4); memcpy(&object,bytes,8);
    EVObject ref;
    return serial==weak.serial && ev_object(object,&ref) && ref.index==weak.index ? object : 0;
}
static uint64_t ew_find_component_in(uint64_t weapon, const char *name, uint32_t size, uint32_t list_offset) {
    EVArray list={0};
    if (!ew_bind_serial(weapon) || !ev_read(weapon+list_offset,&list,sizeof(list)) || !is_any_ptr(list.data) ||
        list.count<0 || list.count>128 || list.capacity<list.count || list.capacity>4096) return 0;
    for (int i=0;i<list.count;i++) {
        uint64_t component=ew_resolve_weak(list.data,i);
        if (component && ew_is_a(component,name,size) && ew_owned_by(component,weapon)) return component;
    }
    return 0;
}
static uint64_t ew_find_component(uint64_t weapon, const char *name, uint32_t size) {
    return ew_find_component_in(weapon,name,size,WB_OFF_FUNC_LIST);
}
static uint64_t ew_get_current_weapon(uint64_t pawn) {
    EVObject ref; uint8_t dead=1;
    if (!ev_object(pawn,&ref) || !ev_read(pawn+OFF_CHARACTER_LAST_IS_DEAD,&dead,1) || (dead&1)) return 0;
    uint64_t wm=ev_ptr(pawn+WM_OFF_FROM_PAWN);
    if (!ew_is_a(wm,"WeaponManagerComponent",0x248) || ev_ptr(wm+0x148)!=pawn) return 0;
    EVArray list={0};
    if (!ev_read(wm+WM_OFF_WEAPON_LIST,&list,sizeof(list)) ||
        !is_any_ptr(list.data) || list.count<1 || list.count>16 ||
        list.capacity<list.count || list.capacity>128) return 0;
    uint64_t weapon=ev_ptr(wm+WM_OFF_CURRENT_ACTIVE);
    int listed=0;
    for (int i=0;i<list.count;i++) if (ev_ptr(list.data+(uint64_t)i*8)==weapon) listed=1;
    /* Never substitute a holstered weapon for an empty active slot. */
    if (!listed || !weapon || weapon==ev_ptr(wm+0x1d0) || !ew_is_a(weapon,"WeaponBase",0xbe8) ||
        ev_ptr(weapon+0xa60)!=pawn) return 0;
    return weapon;
}
static void ew_restore_group(EWGroup *group) {
    if (ew_same(&group->owner) && ew_same(&group->source) &&
        (!group->source_link || ev_ptr(group->source.address+group->source_link)==group->owner.address)) for (int i=0;i<group->count;i++) {
        EWField *field=&group->fields[i]; unsigned char current[4];
        uint64_t address=group->owner.address+field->offset;
        if (!ev_read(address,current,field->size)) continue;
        if (field->size==1) {
            if (!(current[0]&field->mask)) {
                current[0]=(current[0]&~field->mask)|(field->before[0]&field->mask);
                ew_write(address,current,1);
            }
        } else {
            if (!memcmp(current,field->applied,4)) ew_write(address,field->before,4);
        }
    }
    memset(group,0,sizeof(*group));
}
static int ew_begin(EWGroup *group, uint64_t object, uint64_t source, uint32_t link) {
    if (group->owner.address==object && group->source.address==source &&
        ew_same(&group->owner) && ew_same(&group->source)) return 1;
    ew_restore_group(group);
    group->source_link=link;
    return ew_object(object,&group->owner) && ew_object(source,&group->source);
}
static int ew_set_value(EWGroup *group, uint32_t offset, uint8_t size, uint8_t mask, float target) {
    unsigned char current[4]={0}, desired[4]={0};
    if (size==4) memcpy(desired,&target,4);
    if (!ew_same(&group->owner) || !ew_same(&group->source) ||
        (group->source_link && ev_ptr(group->source.address+group->source_link)!=group->owner.address) ||
        !ev_read(group->owner.address+offset,current,size)) return 0;
    if (size==4) { float value; memcpy(&value,current,4); if (!isfinite(value) || fabsf(value)>1e8f) return 0; }
    else desired[0]=current[0]&~mask;
    EWField *field=NULL;
    for (int i=0;i<group->count;i++) if (group->fields[i].offset==offset) { field=&group->fields[i]; break; }
    if (!field) {
        if (group->count==EW_MAX_FIELDS) return 0;
        field=&group->fields[group->count++]; field->offset=offset; field->size=size; field->mask=mask;
        memcpy(field->before,current,size);
    } else if (size==4 && memcmp(current,desired,4)) {
        ew_host_rewrites++;
        /* Retain the host's latest recalculated attachment configuration. */
        memcpy(field->before,current,4);
    }
    memcpy(field->applied,desired,size);
    return !memcmp(current,desired,size) || ew_write(group->owner.address+offset,desired,size);
}
static int ew_set(EWGroup *group, uint32_t offset, uint8_t size, uint8_t mask, float target) {
    int ok=ew_set_value(group,offset,size,mask,target);
    if (!ok && !ew_bad_field) ew_bad_field=offset;
    return ok;
}
static int ew_float(EWGroup *group, uint32_t offset) { return ew_set(group,offset,4,0,0); }
static int ew_flag(EWGroup *group, uint32_t offset, uint8_t mask) { return ew_set(group,offset,1,mask,0); }

static int ew_apply_no_recoil(uint64_t weapon) {
    ew_bad_field=0;
    uint64_t shake=ev_ptr(weapon+WB_OFF_FIRESHAKE_CACHE);
    if (!ew_is_a(shake,"WeaponFuncComponentFireShake",0x9c0) || !ew_owned_by(shake,weapon))
        shake=ew_find_component(weapon,"WeaponFuncComponentFireShake",0x9c0);
    uint64_t recoil=shake ? ev_ptr(shake+FS_OFF_FIRE_RECOIL) : 0;
    uint64_t source=shake; uint32_t link=FS_OFF_FIRE_RECOIL;
    /* FireShake caches only the DFM implementation. Other weapons keep their
     * actual recoil strategy on GPWeaponDataComponentFiring.FireRecoilMethod. */
    if (shake) {
        uint64_t firing=ev_ptr(shake+0x960);
        if (!ew_is_a(firing,"GPWeaponDataComponentFiring",0xa08) || !ew_owned_by(firing,weapon))
            firing=ew_find_component_in(weapon,"GPWeaponDataComponentFiring",0xa08,0xac8);
        ew_firing_seen=firing;
        if (ew_is_a(firing,"GPWeaponDataComponentFiring",0xa08) && ew_owned_by(firing,weapon)) {
            uint64_t method=ev_ptr(firing+0x578);
            if (method) { recoil=method; source=firing; link=0x578; }
        }
    }
    ew_shake_seen=shake; ew_recoil_seen=recoil;
    static uint64_t diagnosed;
    if (recoil && recoil!=diagnosed) {
        diagnosed=recoil;
        EVObject ref; char type[96]="unknown", line[200];
        if (ev_object(recoil,&ref)) resolve_fname(read_i32(ref.cls+OFF_UOBJECT_NAME),type,sizeof(type));
        snprintf(line,sizeof(line),"weapon implementation: %s object=%lx",type,(unsigned long)recoil); _diag(line);
    }
    int dfm=ew_is_a(recoil,"WeaponFireRecoilDFM",0x1db0);
    int pubgm=!dfm && ew_is_a(recoil,"WeaponFireRecoilPUBGM",0xb8);
    if (!shake || (!dfm && !pubgm)) {
        ew_restore_group(&ew_recoil_group); ew_restore_group(&ew_shake_group); ew_restore_group(&ew_kick_group); return 0;
    }
    if (!ew_begin(&ew_recoil_group,recoil,source,link) || !ew_begin(&ew_shake_group,shake,shake,0)) return 0;
    int ok=1;
    const unsigned configs[]={0x88,0xd48}, states[]={0,0x368,0x6c8};
    for (unsigned c=0;dfm && c<2;c++) {
        unsigned cfg=configs[c];
        for (unsigned s=0;s<3;s++) {
            ok &= ew_float(&ew_recoil_group,cfg+states[s]+0x48); /* Horizontal.Scale */
            ok &= ew_float(&ew_recoil_group,cfg+states[s]+0xa8); /* Vertical.Scale */
        }
        /* Per-shot pose factors are additive percentage modifiers, not raw
         * amplitudes. Native aggregation starts at 1 and clamps negatives to
         * zero (+0xBA42490, +0xBA42730). A full negative adjustment cancels
         * the cached pattern; zero merely leaves its original size unchanged. */
        for (unsigned pose=0;pose<6;pose++) ok &= ew_set(&ew_recoil_group,cfg+0xa28+pose*4,4,0,-1.0f);
        /* Reflected amplitudes/factors, not opaque recoil runtime memory. */
        const unsigned amplitudes[]={0xa40,0xa44,0xb08,0xb20,0xb38,0xbe8,0xc98,0xc9c,0xca0,0xca4,0xcb0,0xcb4};
        for (unsigned a=0;a<sizeof(amplitudes)/sizeof(*amplitudes);a++) ok &= ew_float(&ew_recoil_group,cfg+amplitudes[a]);
        ok &= ew_flag(&ew_recoil_group,cfg+0xab0,0xff);
    }
    if (pubgm) {
        const unsigned legacy_configs[]={0x48,0x80};
        for (unsigned c=0;c<2;c++) for (unsigned f=4;f<=0x2c;f+=4)
            ok &= ew_float(&ew_recoil_group,legacy_configs[c]+f);
    }
    /* Firing shake channels: preserve curve pointers, arrays and maps. */
    for (unsigned axis=0;axis<6;axis++) ok &= ew_flag(&ew_shake_group,0x288+0x30+axis*0x70+0xc,1);
    const unsigned flags[]={0x2d4,0x2d5,0x304,0x305,0x330,0x331,0x332,0x378,0x3c8,0x418};
    for (unsigned i=0;i<sizeof(flags)/sizeof(*flags);i++) ok &= ew_flag(&ew_shake_group,0x288+flags[i],0xff);
    const unsigned runtime[]={0x7a8,0x828,0x8a8};
    for (unsigned i=0;i<3;i++) ok &= ew_flag(&ew_shake_group,runtime[i],0xff);
    uint64_t kick=ew_find_component(weapon,"WeaponFuncComponentGunKick",0x360);
    if (kick) {
        ok &= ew_begin(&ew_kick_group,kick,kick,0);
        /* Native camera/weapon transform outputs multiply by these scalars
         * at +0xB9D02EC and +0xB9D06C8. Preserve spring state and curve refs. */
        ok &= ew_float(&ew_kick_group,0x25c);
        ok &= ew_float(&ew_kick_group,0x260);
    } else ew_restore_group(&ew_kick_group);
    if (!ok) { ew_restore_group(&ew_recoil_group); ew_restore_group(&ew_shake_group); ew_restore_group(&ew_kick_group); return 0; }
    atomic_fetch_add(&ew_recoil_zeros,1);
    return 1;
}
static int ew_apply_no_spread(uint64_t weapon) {
    uint64_t component=ew_find_component(weapon,"WeaponFuncComponentSpread",0x518);
    uint64_t method=component ? ev_ptr(component+FC_SPREAD_OFF_METHOD) : 0;
    if (!ew_is_a(method,"WeaponFireSpreadDFM",0x7d8)) { ew_restore_group(&ew_spread_group); return 0; }
    if (!ew_begin(&ew_spread_group,method,component,FC_SPREAD_OFF_METHOD)) return 0;
    int ok=1; const unsigned configs[]={0x50,0x1d0};
    for (unsigned c=0;c<2;c++) {
        unsigned cfg=configs[c];
        for (unsigned f=4;f<0x1c;f+=4) ok &= ew_float(&ew_spread_group,cfg+f);
        ok &= ew_float(&ew_spread_group,cfg+0x68);
    }
    /* Native live spread getter (+0xBBD76EC) interpolates cached hip/aim
     * half-angles at +0x3D0/+0x598; current spread is +0x760 (+0xF1BFB58).
     * The reflected authoring configs alone do not clear these live caches. */
    const unsigned cached[]={0x3d0,0x3d4,0x598,0x59c,0x590,0x594,0x758,0x75c,0x760,0x764};
    for (unsigned i=0;i<sizeof(cached)/sizeof(*cached);i++) ok &= ew_float(&ew_spread_group,cached[i]);
    if (!ok) { ew_restore_group(&ew_spread_group); return 0; }
    atomic_fetch_add(&ew_spread_zeros,1);
    return 1;
}
static void ew_release(void) {
    ew_restore_group(&ew_recoil_group); ew_restore_group(&ew_shake_group); ew_restore_group(&ew_kick_group); ew_restore_group(&ew_spread_group);
}
static void ew_update(uint64_t pawn) {
    int nr=atomic_load(&ew_no_recoil), ns=atomic_load(&ew_no_spread);
    if (!nr) { ew_restore_group(&ew_recoil_group); ew_restore_group(&ew_shake_group); ew_restore_group(&ew_kick_group); }
    if (!ns) ew_restore_group(&ew_spread_group);
    if (!nr && !ns) return;
    uint64_t weapon=ew_get_current_weapon(pawn);
    if (!weapon || !ew_bind_serial(weapon)) {
        ew_release();
        if (ew_diag_seq++%200==0) {
            uint64_t wm=ev_ptr(pawn+WM_OFF_FROM_PAWN);
            char line[160]; snprintf(line,sizeof(line),"weapon: unavailable resolved=%lx current=%lx serial_offset=%d",
                (unsigned long)weapon,(unsigned long)ev_ptr(wm+WM_OFF_CURRENT_ACTIVE),ew_serial_offset); _diag(line);
        }
        return;
    }
    int recoil=nr ? ew_apply_no_recoil(weapon) : 0;
    int spread=ns ? ew_apply_no_spread(weapon) : 0;
    if (ew_diag_seq++%200==0) {
        char line[200];
        snprintf(line,sizeof(line),"weapon: wpn=%lx recoil=%d spread=%d fields=%d/%d/%d kick=%d applied=%u/%u",
            (unsigned long)weapon,recoil,spread,ew_recoil_group.count,ew_shake_group.count,ew_spread_group.count,
            ew_kick_group.count,
            atomic_load(&ew_recoil_zeros),atomic_load(&ew_spread_zeros));
        _diag(line);
        if (ns && spread) {
            float extent[6]={0}; uint64_t s=ew_spread_group.owner.address;
            ev_read(s+0x3d0,extent,8);ev_read(s+0x598,extent+2,8);ev_read(s+0x760,extent+4,8);
            snprintf(line,sizeof(line),"weapon spread output: hip=%.4f,%.4f aim=%.4f,%.4f current=%.4f,%.4f",
                extent[0],extent[1],extent[2],extent[3],extent[4],extent[5]); _diag(line);
        }
        if (nr && recoil) {
            uint64_t r=ew_recoil_group.owner.address, f=ew_recoil_group.source.address;
            float values[6]={0};
            ev_read(r+0x1c40,values,8); ev_read(r+0x1a94,values+2,8); ev_read(r+0x88+0xa28,values+4,8);
            snprintf(line,sizeof(line),"weapon runtime: source=%lx self_dc=%lx game_dc=%lx shot=%d target=%.3f,%.3f shake=%.3f,%.3f pose=%.2f,%.2f rewrites=%u",
                (unsigned long)f,(unsigned long)ev_ptr(r+0x30),(unsigned long)ev_ptr(r+0x48),read_i32(f+0x658),
                values[0],values[1],values[2],values[3],values[4],values[5],ew_host_rewrites); _diag(line);
            snprintf(line,sizeof(line),"weapon peers: gp=%lx fp=%lx tp=%lx weapon=%lx fp=%lx tp=%lx",
                (unsigned long)ev_ptr(weapon+0xd18),(unsigned long)ev_ptr(weapon+0xd28),(unsigned long)ev_ptr(weapon+0xd38),
                (unsigned long)ev_ptr(weapon+0xd58),(unsigned long)ev_ptr(weapon+0xd68),(unsigned long)ev_ptr(weapon+0xd78)); _diag(line);
        }
        if (nr && !recoil) {
            char shake_type[80]="unresolved", recoil_type[80]="unresolved";
            EVObject object;
            if (ev_object(ew_shake_seen,&object)) resolve_fname(read_i32(object.cls+OFF_UOBJECT_NAME),shake_type,sizeof(shake_type));
            if (ev_object(ew_recoil_seen,&object)) resolve_fname(read_i32(object.cls+OFF_UOBJECT_NAME),recoil_type,sizeof(recoil_type));
            snprintf(line,sizeof(line),"weapon recoil binding: shake=%lx %.60s recoil=%lx %.60s bad_field=%x serial_off=%x",
                (unsigned long)ew_shake_seen,shake_type,(unsigned long)ew_recoil_seen,recoil_type,ew_bad_field,ew_serial_offset);
            _diag(line);
            char firing_type[80]="unresolved";
            if (ev_object(ew_firing_seen,&object)) resolve_fname(read_i32(object.cls+OFF_UOBJECT_NAME),firing_type,sizeof(firing_type));
            snprintf(line,sizeof(line),"weapon recoil source: firing=%lx %.60s method=%lx flags=%x",
                (unsigned long)ew_firing_seen,firing_type,(unsigned long)ev_ptr(ew_firing_seen+0x578),
                (unsigned)read_i32(ew_recoil_seen+0x18));
            _diag(line);
        }
    }
}
#endif
