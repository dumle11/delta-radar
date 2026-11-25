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
#define EW_SPREAD_CAP 0.000001f /* positive: zero is the native unlimited sentinel */
#define EW_RECOIL_CANCEL -1000.0f /* additive channel clamps negative sums to zero */
static _Atomic int ew_no_recoil, ew_no_spread, ew_no_sway;
static _Atomic unsigned ew_recoil_zeros, ew_spread_zeros;
static unsigned ew_diag_seq;
static int ew_serial_offset = -1;
static uint64_t ew_shake_seen, ew_recoil_seen, ew_firing_seen;
static uint32_t ew_bad_field;
static unsigned ew_host_rewrites;
static __thread SelfMemoryReader ew_writer = {-1,-1};
typedef struct { uint32_t offset; uint8_t size, mask; unsigned char before[4], applied[4]; } EWField;
typedef struct { EVObject owner, source; uint32_t source_link; int count; EWField fields[EW_MAX_FIELDS]; } EWGroup;
static EWGroup ew_recoil_group, ew_shake_group, ew_kick_group, ew_sway_group, ew_spread_group;
static EWGroup ew_camera_groups[3];
static EWGroup ew_spread_gate_group;

static const unsigned char _ew_WMC[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x18,0x34,0x3b,0x34,0x32,0x30,0x27,0x16,0x3a,0x38,0x25,0x3a,0x3b,0x30,0x3b,0x21};
static const unsigned char _ew_WB[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x17,0x34,0x26,0x30};
static const unsigned char _ew_WFCFS[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x13,0x20,0x3b,0x36,0x16,0x3a,0x38,0x25,0x3a,0x3b,0x30,0x3b,0x21,0x13,0x3c,0x27,0x30,0x06,0x3d,0x34,0x3e,0x30};
static const unsigned char _ew_GPWDCF[] = {0x12,0x05,0x02,0x30,0x34,0x25,0x3a,0x3b,0x11,0x34,0x21,0x34,0x16,0x3a,0x38,0x25,0x3a,0x3b,0x30,0x3b,0x21,0x13,0x3c,0x27,0x3c,0x3b,0x32};
static const unsigned char _ew_WFRD[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x13,0x3c,0x27,0x30,0x07,0x30,0x36,0x3a,0x3c,0x39,0x11,0x13,0x18};
static const unsigned char _ew_WFRP[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x13,0x3c,0x27,0x30,0x07,0x30,0x36,0x3a,0x3c,0x39,0x05,0x00,0x17,0x12,0x18};
static const unsigned char _ew_WFCGK[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x13,0x20,0x3b,0x36,0x16,0x3a,0x38,0x25,0x3a,0x3b,0x30,0x3b,0x21,0x12,0x20,0x3b,0x1e,0x3c,0x36,0x3e};
static const unsigned char _ew_WFCGS[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x13,0x20,0x3b,0x36,0x16,0x3a,0x38,0x25,0x3a,0x3b,0x30,0x3b,0x21,0x12,0x20,0x3b,0x06,0x22,0x34,0x2c};
static const unsigned char _ew_WFCS[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x13,0x20,0x3b,0x36,0x16,0x3a,0x38,0x25,0x3a,0x3b,0x30,0x3b,0x21,0x06,0x25,0x27,0x30,0x34,0x31};
static const unsigned char _ew_WFSD[] = {0x02,0x30,0x34,0x25,0x3a,0x3b,0x13,0x3c,0x27,0x30,0x06,0x25,0x27,0x30,0x34,0x31,0x11,0x13,0x18};
static const unsigned char _ew_PCM[] = {0x05,0x39,0x34,0x2c,0x30,0x27,0x16,0x34,0x38,0x30,0x27,0x34,0x18,0x34,0x3b,0x34,0x32,0x30,0x27};
static const unsigned char _ew_GCMWR[] = {0x12,0x05,0x16,0x34,0x38,0x30,0x27,0x34,0x18,0x3a,0x31,0x3c,0x33,0x3c,0x30,0x27,0x0a,0x02,0x30,0x34,0x25,0x3a,0x3b,0x07,0x30,0x36,0x3a,0x3c,0x39};
static const unsigned char _ew_GCMWGS[] = {0x12,0x05,0x16,0x34,0x38,0x30,0x27,0x34,0x18,0x3a,0x31,0x3c,0x33,0x3c,0x30,0x27,0x0a,0x02,0x30,0x34,0x25,0x3a,0x3b,0x12,0x20,0x3b,0x06,0x22,0x34,0x2c};
static const unsigned char _ew_GCMF[] = {0x12,0x05,0x16,0x34,0x38,0x30,0x27,0x34,0x18,0x3a,0x31,0x3c,0x33,0x3c,0x30,0x27,0x0a,0x13,0x3c,0x27,0x30};
static __attribute__((noinline)) const char *ew_d(const unsigned char *enc, int n) {
    static __thread char _ew_buf[4][96];
    static __thread int _ew_idx;
    char *out = _ew_buf[_ew_idx++ & 3];
    volatile unsigned char k = 0x55;
    for (int i = 0; i < n; i++) out[i] = enc[i] ^ k;
    out[n] = 0;
    return out;
}
#define EW_WMC ew_d(_ew_WMC,22)
#define EW_WB ew_d(_ew_WB,10)
#define EW_WFCFS ew_d(_ew_WFCFS,28)
#define EW_GPWDCF ew_d(_ew_GPWDCF,27)
#define EW_WFRD ew_d(_ew_WFRD,19)
#define EW_WFRP ew_d(_ew_WFRP,21)
#define EW_WFCGK ew_d(_ew_WFCGK,26)
#define EW_WFCGS ew_d(_ew_WFCGS,26)
#define EW_WFCS ew_d(_ew_WFCS,25)
#define EW_WFSD ew_d(_ew_WFSD,19)
#define EW_PCM ew_d(_ew_PCM,19)
#define EW_GCMWR ew_d(_ew_GCMWR,29)
#define EW_GCMWGS ew_d(_ew_GCMWGS,30)
#define EW_GCMF ew_d(_ew_GCMF,21)

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
    if (!ew_is_a(wm,EW_WMC,0x248) || ev_ptr(wm+0x148)!=pawn) return 0;
    EVArray list={0};
    if (!ev_read(wm+WM_OFF_WEAPON_LIST,&list,sizeof(list)) ||
        !is_any_ptr(list.data) || list.count<1 || list.count>16 ||
        list.capacity<list.count || list.capacity>128) return 0;
    uint64_t weapon=ev_ptr(wm+WM_OFF_CURRENT_ACTIVE);
    int listed=0;
    for (int i=0;i<list.count;i++) if (ev_ptr(list.data+(uint64_t)i*8)==weapon) listed=1;
    /* Never substitute a holstered weapon for an empty active slot. */
    if (!listed || !weapon || weapon==ev_ptr(wm+0x1d0) || !ew_is_a(weapon,EW_WB,0xbe8) ||
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

static int ew_apply_sway_component(uint64_t sway) {
    if(!ew_begin(&ew_sway_group,sway,sway,0)) return 0;
    const unsigned scales[]={0x1cc,0x260,0xf80,0x1350};
    for(unsigned i=0;i<4;i++) if(!ew_float(&ew_sway_group,scales[i])) {
        ew_restore_group(&ew_sway_group);return 0;
    }
    return 1;
}
static int ew_apply_no_sway(uint64_t weapon) {
    uint64_t sway=ew_find_component(weapon,EW_WFCGS,0x1690);
    if(!sway) {ew_restore_group(&ew_sway_group);return 0;}
    return ew_apply_sway_component(sway);
}
static int ew_apply_no_recoil(uint64_t weapon) {
    ew_bad_field=0;
    uint64_t shake=ev_ptr(weapon+WB_OFF_FIRESHAKE_CACHE);
    if (!ew_is_a(shake,EW_WFCFS,0x9c0) || !ew_owned_by(shake,weapon))
        shake=ew_find_component(weapon,EW_WFCFS,0x9c0);
    uint64_t recoil=shake ? ev_ptr(shake+FS_OFF_FIRE_RECOIL) : 0;
    uint64_t source=shake; uint32_t link=FS_OFF_FIRE_RECOIL;
    /* FireShake caches only the DFM implementation. Other weapons keep their
     * actual recoil strategy on GPWeaponDataComponentFiring.FireRecoilMethod. */
    if (shake) {
        uint64_t firing=ev_ptr(shake+0x960);
        if (!ew_is_a(firing,EW_GPWDCF,0xa08) || !ew_owned_by(firing,weapon))
            firing=ew_find_component_in(weapon,EW_GPWDCF,0xa08,0xac8);
        ew_firing_seen=firing;
        if (ew_is_a(firing,EW_GPWDCF,0xa08) && ew_owned_by(firing,weapon)) {
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
    int dfm=ew_is_a(recoil,EW_WFRD,0x1db0);
    int pubgm=!dfm && ew_is_a(recoil,EW_WFRP,0xb8);
    if (!shake || (!dfm && !pubgm)) {
        ew_restore_group(&ew_recoil_group); ew_restore_group(&ew_shake_group); ew_restore_group(&ew_kick_group); ew_restore_group(&ew_sway_group); return 0;
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
         * zero (+0xBA42490, +0xBA42730). Include headroom for other attachment
         * terms in the same additive channel; -1 alone leaves their residue. */
        for (unsigned pose=0;pose<6;pose++) ok &= ew_set(&ew_recoil_group,cfg+0xa28+pose*4,4,0,EW_RECOIL_CANCEL);
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
    for (unsigned axis=0;axis<6;axis++) {
        unsigned item=0x288+0x30+axis*0x70;
        ok &= ew_flag(&ew_shake_group,item+0xc,1);
        const unsigned amplitudes[]={8,0x14,0x18,0x54,0x58};
        for (unsigned i=0;i<5;i++) ok &= ew_float(&ew_shake_group,item+amplitudes[i]);
    }
    const unsigned flags[]={0x2d4,0x2d5,0x304,0x305,0x330,0x331,0x332,0x378,0x3c8,0x418};
    for (unsigned i=0;i<sizeof(flags)/sizeof(*flags);i++) ok &= ew_flag(&ew_shake_group,0x288+flags[i],0xff);
    const unsigned runtime[]={0x7a8,0x828,0x8a8};
    for (unsigned i=0;i<3;i++) {
        ok &= ew_flag(&ew_shake_group,runtime[i],0xff);
        for (unsigned a=0xc;a<=0x18;a+=4) ok &= ew_float(&ew_shake_group,runtime[i]+a);
    }
    /* Reflected cached camera roll outputs; their time, identity, last-view
     * rotation and arrays remain intact. Flags do not clear a running shake. */
    const unsigned roll_outputs[]={0x470,0x474,0x478,0x47c,0x480,0x48c,0x490,0x498,0x49c,0x4a0,0x4a4,0x4a8};
    for (unsigned i=0;i<12;i++) ok &= ew_float(&ew_shake_group,0x288+roll_outputs[i]);
    uint64_t kick=ew_find_component(weapon,EW_WFCGK,0x360);
    if (kick) {
        ok &= ew_begin(&ew_kick_group,kick,kick,0);
        /* Native camera/weapon transform outputs multiply by these scalars
         * at +0xB9D02EC and +0xB9D06C8. Preserve spring state and curve refs. */
        ok &= ew_float(&ew_kick_group,0x25c);
        ok &= ew_float(&ew_kick_group,0x260);
    } else ew_restore_group(&ew_kick_group);
    uint64_t sway=ew_find_component(weapon,EW_WFCGS,0x1690);
    if (sway) ok &= ew_apply_sway_component(sway);
    else ew_restore_group(&ew_sway_group);
    if (!ok) { ew_restore_group(&ew_recoil_group); ew_restore_group(&ew_shake_group); ew_restore_group(&ew_kick_group); ew_restore_group(&ew_sway_group); return 0; }
    atomic_fetch_add(&ew_recoil_zeros,1);
    return 1;
}
static int ew_apply_no_spread(uint64_t weapon) {
    uint64_t component=ew_find_component(weapon,EW_WFCS,0x518);
    uint64_t method=component ? ev_ptr(component+FC_SPREAD_OFF_METHOD) : 0;
    if (!ew_is_a(method,EW_WFSD,0x7d8)) { ew_restore_group(&ew_spread_group); ew_restore_group(&ew_spread_gate_group); return 0; }
    if (!ew_begin(&ew_spread_group,method,component,FC_SPREAD_OFF_METHOD)) return 0;
    int ok=1; const unsigned configs[]={0x50,0x1d0};
    for (unsigned c=0;c<2;c++) {
        unsigned cfg=configs[c];
        for (unsigned f=4;f<0x1c;f+=4) ok &= ew_set(&ew_spread_group,cfg+f,4,0,f>=0x14 ? EW_SPREAD_CAP : 0);
        ok &= ew_float(&ew_spread_group,cfg+0x68);
    }
    /* Distribution and modifier radii are separate from the base cone.
     * Preserve sampling counts/ratios and the custom-position array. */
    const unsigned modifiers[]={0x350,0x390};
    for(unsigned c=0;c<2;c++) {
        ok &= ew_float(&ew_spread_group,modifiers[c]+4); /* MaxRotateAngle */
        ok &= ew_float(&ew_spread_group,modifiers[c]+8); /* CenterSlideValue */
        ok &= ew_float(&ew_spread_group,modifiers[c]+0x2c); /* RandomRadius.X */
        ok &= ew_float(&ew_spread_group,modifiers[c]+0x30); /* RandomRadius.Y */
    }
    /* Native live spread getter (+0xBBD76EC) interpolates cached hip/aim
     * half-angles at +0x3D0/+0x598; current spread is +0x760 (+0xF1BFB58).
     * The reflected authoring configs alone do not clear these live caches. */
    /* Init copies SpreadMax/SpreadMaxFinal into runtime +0x2C/+0x34
     * (+0xBBD5844). Clear these bounds so the engine's own per-frame clamp
     * produces zero between scanner updates, including the firing frame. */
    const unsigned cached[]={0x3d0,0x3d4,0x3fc,0x400,0x404,0x408,
        0x598,0x59c,0x5c4,0x5c8,0x5cc,0x5d0,0x590,0x594,0x758,0x75c,0x760,0x764};
    for (unsigned i=0;i<sizeof(cached)/sizeof(*cached);i++) {
        unsigned off=cached[i];
        /* +0xBBD489C selects a large fallback if the final maximum is zero.
         * A positive subpixel cap keeps the native clamp enabled. */
        int final_max=off==0x404 || off==0x408 || off==0x5cc || off==0x5d0;
        ok &= ew_set(&ew_spread_group,off,4,0,final_max ? EW_SPREAD_CAP : 0);
    }
    /* Native component enable gate: getter +0xBBE4C94 and shot callbacks
     * (+0xBBE4DF0/+0xBBE4FAC/+0xBBE515C) skip the strategy when false. This
     * prevents within-frame dispersion that scanner-only cache writes miss. */
    ok &= ew_begin(&ew_spread_gate_group,component,component,0);
    ok &= ew_flag(&ew_spread_gate_group,0x1e0,0xff);
    if (!ok) { ew_restore_group(&ew_spread_group); ew_restore_group(&ew_spread_gate_group); return 0; }
    atomic_fetch_add(&ew_spread_zeros,1);
    return 1;
}
static void ew_camera_release(void) {
    for(unsigned i=0;i<3;i++) ew_restore_group(&ew_camera_groups[i]);
}
static void ew_camera_update(uint64_t pawn,int recoil,int sway) {
    uint64_t controller=ev_ptr(pawn+0x3a8),manager=ev_ptr(controller+0x408);
    EVArray list={0};
    if (!ew_is_a(manager,EW_PCM,0x4270) || ev_ptr(manager+0x370)!=controller ||
        !ev_read(manager+0x4260,&list,sizeof(list)) || !is_any_ptr(list.data) ||
        list.count<0 || list.count>64 || list.capacity<list.count || list.capacity>256) {
        ew_camera_release();return;
    }
    char n0[30],n1[31],n2[22];
    ev_decode(n0,_ew_GCMWR,29); ev_decode(n1,_ew_GCMWGS,30); ev_decode(n2,_ew_GCMF,21);
    const char *names[]={n0,n1,n2};
    for(unsigned k=0;k<3;k++) {
        if(!(k==1?sway:recoil)) {ew_restore_group(&ew_camera_groups[k]);continue;}
        uint64_t found=0;
        for(int i=0;i<list.count;i++) {
            uint64_t modifier=ev_ptr(list.data+(uint64_t)i*8);
            if(ew_is_a(modifier,names[k],k==2?0x50:0x48) && ev_ptr(modifier+0x30)==manager) {found=modifier;break;}
        }
        if (!found || !ew_begin(&ew_camera_groups[k],found,manager,0) ||
            !ew_flag(&ew_camera_groups[k],k==2?0x48:0x44,0xff)) ew_restore_group(&ew_camera_groups[k]);
    }
}
static void ew_release(void) {
    ew_restore_group(&ew_recoil_group); ew_restore_group(&ew_shake_group); ew_restore_group(&ew_kick_group); ew_restore_group(&ew_sway_group); ew_restore_group(&ew_spread_group);
    ew_camera_release();
    ew_restore_group(&ew_spread_gate_group);
}
/* Read the game's completed shot record, without changing its fire packet.
 * Comparing these angles separates dispersion from camera/weapon animation. */
static void ew_log_shot(uint64_t weapon) {
    static uint64_t last_source,last_id; static unsigned logged;
    uint64_t source=ew_firing_seen;
    if (!ew_is_a(source,EW_GPWDCF,0xa08) || !ew_owned_by(source,weapon)) return;
    uint64_t id=ev_ptr(source+0x4b0);
    if (last_source!=source) { last_source=source;last_id=0;logged=0; }
    if (!id || id==last_id) return;
    last_id=id;
    atomic_store(&ea_shot_this_frame, 1);
    { struct timespec _ts; clock_gettime(CLOCK_MONOTONIC, &_ts);
      atomic_store(&ea_last_fire_ms, (int64_t)_ts.tv_sec * 1000 + _ts.tv_nsec / 1000000); }
    if (logged>=256) return;
    EVArray bullets={0};float rotations[6],view[4],kick[2];
    if (!ev_read(source+0x4d8,&bullets,sizeof(bullets)) || !is_any_ptr(bullets.data) ||
        bullets.count<1 || bullets.count>32 || bullets.capacity<bullets.count || bullets.capacity>128 ||
        !ev_read(bullets.data+0x20,rotations,sizeof(rotations)) ||
        !ev_read(source+0x4fc,view,sizeof(view)) || !ev_read(source+0x514,kick,sizeof(kick)) ||
        ev_ptr(source+0x4b0)!=id) return;
    for (unsigned i=0;i<6;i++) if (!isfinite(rotations[i]) || fabsf(rotations[i])>3600) return;
    logged++;
    float control[2]={0},spread[2]={0};
    uint64_t controller=ev_ptr(ev_ptr(weapon+0xa60)+0x3a8);
    ev_read(controller+0x3d8,control,8);
    if (ew_same(&ew_spread_group.owner)) ev_read(ew_spread_group.owner.address+0x760,spread,8);
    char line[256];snprintf(line,sizeof(line),"weapon shot: id=%lx spawn=%.5f,%.5f fire=%.5f,%.5f control=%.5f,%.5f spread=%.5f,%.5f kick=%.5f,%.5f",
        (unsigned long)id,rotations[0],rotations[1],rotations[3],rotations[4],control[0],control[1],spread[0],spread[1],kick[0],kick[1]);_diag(line);
}
static void ew_update(uint64_t pawn) {
    atomic_store(&ea_shot_this_frame, 0);
    int nr=atomic_load(&ew_no_recoil), ns=atomic_load(&ew_no_spread), nw=atomic_load(&ew_no_sway);
    if (!nr) { ew_restore_group(&ew_recoil_group); ew_restore_group(&ew_shake_group); ew_restore_group(&ew_kick_group); }
    if (!nr && !nw) {ew_restore_group(&ew_sway_group);ew_camera_release();}
    if (!ns) {ew_restore_group(&ew_spread_group);ew_restore_group(&ew_spread_gate_group);}
    int need_shot_track = atomic_load(&ea_aim_lock_mode) == 1;
    if (!nr && !ns && !nw && !need_shot_track) return;
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
    {
        uint64_t source=ew_firing_seen;
        if (!source || !ew_is_a(source,EW_GPWDCF,0xa08) || !ew_owned_by(source,weapon)) {
            uint64_t shake=ev_ptr(weapon+WB_OFF_FIRESHAKE_CACHE);
            if (shake) {
                uint64_t firing=ev_ptr(shake+0x960);
                if (!ew_is_a(firing,EW_GPWDCF,0xa08) || !ew_owned_by(firing,weapon))
                    firing=ew_find_component_in(weapon,EW_GPWDCF,0xa08,0xac8);
                ew_firing_seen=firing;
            }
        }
    }
    ew_log_shot(weapon);
    if (!nr && !ns && !nw) return;
    int recoil=nr ? ew_apply_no_recoil(weapon) : 0;
    if(nw && (!nr || !recoil)) ew_apply_no_sway(weapon);
    if(nr || nw) ew_camera_update(pawn,nr,nr||nw);
    int spread=ns ? ew_apply_no_spread(weapon) : 0;
    if (ew_diag_seq++%200==0) {
        char line[200];
        snprintf(line,sizeof(line),"weapon: wpn=%lx recoil=%d spread=%d no_sway=%d fields=%d/%d/%d kick=%d sway=%d camera=%d gate=%d applied=%u/%u",
            (unsigned long)weapon,recoil,spread,nw,ew_recoil_group.count,ew_shake_group.count,ew_spread_group.count,
            ew_kick_group.count,ew_sway_group.count,
            ew_camera_groups[0].count+ew_camera_groups[1].count+ew_camera_groups[2].count,
            ew_spread_gate_group.count,
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
