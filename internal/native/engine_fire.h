#ifndef INTERNAL_ENGINE_FIRE_H
#define INTERNAL_ENGINE_FIRE_H
#include <pthread.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>

#define EF_ACTOR_PE_RVA 0x107e2070
#define EF_PE_SLOT 0x45
typedef struct { EVObject object; int32_t serial; } EFRef;
typedef struct { EFRef input, proxy, aim, unaim, progress, is_on; } EFZoomRefs;
static uint64_t ew_find_component_in(uint64_t weapon, const char *name, uint32_t size, uint32_t list_offset);
typedef struct {
    int64_t at;
    int enabled, fire, error, auto_zoom;
    EFZoomRefs zoom;
    EFRef pawn, controller, weapon, target, start, stop;
} EFCommand;
static pthread_mutex_t ef_mutex = PTHREAD_MUTEX_INITIALIZER;
static EFCommand ef_request, ef_active, ef_zoom_session;
static EFZoomRefs ef_zoom_bindings;
static EFRef ef_zoom_weapon;
static int ef_zoom_owned;
static _Atomic unsigned ef_zooms, ef_unzooms;
static _Atomic int ef_zoom_progress;
static EFRef ef_start, ef_stop;
static uint64_t ef_bound_class;
static _Atomic(EVProcessEvent) ef_original;
static _Atomic unsigned ef_status, ef_starts, ef_stops, ef_callbacks, ef_reject;
static _Atomic int ef_game_tid;
static __thread unsigned ef_depth;
static int64_t ef_last_tick;

static int64_t ef_now(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (int64_t)time.tv_sec * 1000 + time.tv_nsec / 1000000;
}
static int ef_ref(uint64_t address, EFRef *out) {
    uint64_t item;
    memset(out, 0, sizeof(*out));
    return ev_object(address, &out->object) && ev_item(out->object.index, &item) &&
        ev_read(item + 0x14, &out->serial, 4) && out->serial >= 0; /* zero is valid until a first weak reference is allocated */
}
static int ef_same(const EFRef *ref) {
    EFRef now;
    return ef_ref(ref->object.address, &now) && now.serial == ref->serial &&
        now.object.index == ref->object.index && now.object.cls == ref->object.cls &&
        now.object.name == ref->object.name;
}
/* Native UFunctions are rooted class metadata. A zero serial can become
 * assigned without replacing the object; preserve a balanced StopFire then. */
static int ef_same_function(const EFRef *ref) {
    EFRef now;
    return ef_ref(ref->object.address, &now) &&
        (!ref->serial || now.serial == ref->serial) && now.object.index == ref->object.index &&
        now.object.cls == ref->object.cls && now.object.name == ref->object.name;
}
static int ef_zoom_valid(const EFCommand *c) {
    const EFZoomRefs *z = &c->zoom;
    return ef_same(&z->input) && ef_same(&z->proxy) &&
        ev_ptr(c->controller.object.address + 0x960) == z->input.object.address &&
        ev_ptr(z->proxy.object.address + 0x140) == c->weapon.object.address &&
        ef_same_function(&z->aim) && ef_same_function(&z->unaim) &&
        ef_same_function(&z->progress) && ef_same_function(&z->is_on);
}
static void ef_zoom_release(void) {
    if (ef_zoom_owned && ev_pe_fn && ef_same(&ef_zoom_session.controller) &&
        ef_same(&ef_zoom_session.pawn) && ef_same(&ef_zoom_session.zoom.input) &&
        ev_ptr(ef_zoom_session.controller.object.address + 0x960) == ef_zoom_session.zoom.input.object.address &&
        ev_ptr(ef_zoom_session.controller.object.address + 0x3f0) == ef_zoom_session.pawn.object.address &&
        ef_same_function(&ef_zoom_session.zoom.unaim)) {
        ev_pe_fn((void *)(uintptr_t)ef_zoom_session.zoom.input.object.address,
                 (void *)(uintptr_t)ef_zoom_session.zoom.unaim.object.address, NULL);
        atomic_fetch_add(&ef_unzooms, 1);
    }
    memset(&ef_zoom_session, 0, sizeof(ef_zoom_session));
    ef_zoom_owned = 0;
    atomic_store(&ef_zoom_progress, 0);
}
static int ef_zoom_gate(const EFCommand *c) {
    if (!ef_zoom_valid(c)) return 0;
    if (!ef_zoom_session.enabled) ef_zoom_session = *c;
    uint8_t on = 0xff;
    float progress = NAN;
    ev_pe_fn((void *)(uintptr_t)c->zoom.proxy.object.address,
             (void *)(uintptr_t)c->zoom.is_on.object.address, &on);
    ev_pe_fn((void *)(uintptr_t)c->zoom.proxy.object.address,
             (void *)(uintptr_t)c->zoom.progress.object.address, &progress);
    if (on > 1 || !isfinite(progress) || progress < 0 || progress > 1.01f) return 0;
    atomic_store(&ef_zoom_progress, (int)(progress * 1000));
    if (!on && !ef_zoom_owned) {
        ev_pe_fn((void *)(uintptr_t)c->zoom.input.object.address,
                 (void *)(uintptr_t)c->zoom.aim.object.address, NULL);
        ef_zoom_owned = 1;
        atomic_fetch_add(&ef_zooms, 1);
        return 0; /* Never fire in the same callback that requests ADS. */
    }
    return on && progress >= 0.99f;
}
static void ef_stop_active(void) {
    if (ef_active.fire && ef_same(&ef_active.pawn) && ef_same_function(&ef_active.stop)) {
        int32_t fire_index = 0;
        ev_pe_fn((void *)(uintptr_t)ef_active.pawn.object.address,
                 (void *)(uintptr_t)ef_active.stop.object.address, &fire_index);
        atomic_fetch_add(&ef_stops, 1);
    }
    memset(&ef_active, 0, sizeof(ef_active));
}
/* Called only after an outermost engine dispatch on the engine-identified GameThread.
 * No engine gameplay calls occur on the scanner or Android UI thread. */
static void ef_tick(void) {
    int64_t now = ef_now();
    if (now - ef_last_tick < 8) return;
    ef_last_tick = now;
    EFCommand command;
    if (pthread_mutex_trylock(&ef_mutex)) return;
    command = ef_request;
    pthread_mutex_unlock(&ef_mutex);
    unsigned reject = 0;
    if (!command.enabled) reject |= 1;
    if (now < command.at || now - command.at > 100) reject |= 2;
    if (!(atomic_load(&ea_config) & (1u << 24)) || !atomic_load(&lib_running_flag)) reject |= 4;
    if (command.enabled) {
        if (!ev_pe_fn || !ef_same(&command.pawn) || !ef_same(&command.controller)) reject |= 8;
        if (ev_ptr(command.controller.object.address + 0x3f0) != command.pawn.object.address ||
            !ea_alive(command.pawn.object.address)) reject |= 16;
        if (!ef_same(&command.weapon) ||
            ev_ptr(ev_ptr(command.pawn.object.address + 0x3b18) + 0x218) != command.weapon.object.address) reject |= 32;
        if (!ef_same_function(&command.start) || !ef_same_function(&command.stop)) reject |= 64;
    }
    atomic_store(&ef_reject, reject);
    int ready = !reject;
    int fire = ready && command.fire && ef_same(&command.target) && ea_alive(command.target.object.address);
    if (ef_active.fire && (!fire || command.pawn.object.address != ef_active.pawn.object.address ||
        command.pawn.serial != ef_active.pawn.serial ||
        command.weapon.object.address != ef_active.weapon.object.address || command.weapon.serial != ef_active.weapon.serial || command.target.object.address != ef_active.target.object.address ||
        command.target.serial != ef_active.target.serial)) ef_stop_active();
    int same_zoom = ef_zoom_session.enabled &&
        ef_zoom_session.pawn.object.address == command.pawn.object.address &&
        ef_zoom_session.pawn.serial == command.pawn.serial &&
        ef_zoom_session.weapon.object.address == command.weapon.object.address &&
        ef_zoom_session.weapon.serial == command.weapon.serial &&
        ef_zoom_session.target.object.address == command.target.object.address &&
        ef_zoom_session.target.serial == command.target.serial;
    if (ef_zoom_session.enabled && (!fire || !command.auto_zoom || !same_zoom)) {
        ef_stop_active();
        ef_zoom_release();
    }
    int zoom_ready = !command.auto_zoom || (fire && ef_zoom_gate(&command));
    if (fire && !zoom_ready) ef_stop_active();
    if (fire && zoom_ready && !ef_active.fire) {
        int32_t fire_index = 0;
        ev_pe_fn((void *)(uintptr_t)command.pawn.object.address,
                 (void *)(uintptr_t)command.start.object.address, &fire_index);
        ef_active = command;
        atomic_fetch_add(&ef_starts, 1);
    }
    atomic_store(&ef_status, ef_active.fire ? 2 : command.error ? 3 : fire && command.auto_zoom ? 4 : ready ? 1 : 0);
    atomic_store(&ea_autoshoot_active, ef_active.fire != 0);
}
static void ef_process_event(void *object, void *function, void *params) {
    ++ef_depth;
    ef_original(object, function, params);
    int tid = (int)syscall(__NR_gettid);
    if (ef_depth == 1 && tid == atomic_load(&ef_game_tid)) {
        atomic_fetch_add(&ef_callbacks, 1);
        ef_tick();
    }
    --ef_depth;
}
/* Exact-build Actor::ProcessEvent entry and slot, verified before mutation.
 * The class table has image lifetime; no per-object shadow tables are freed
 * under the engine. Original dispatch always runs first. */
/* SDK MTAPI_CoreGlobals.GetGGameThreadId exec reads GGameThreadId at
 * +0x2067c028. Validate the accessor instructions before reading that global.
 * IsInGameThread compares this same value with the platform thread ID. */
static int ef_bind_thread(void) {
    const uint32_t signature[] = {0xf9401029, 0xf0036b08, 0xb9402908};
    uint32_t actual[3], tid = 0; uint8_t initialized = 0;
    if (!ev_read(ue4_base + 0x199193c0, actual, sizeof(actual)) ||
        memcmp(actual, signature, sizeof(actual)) ||
        !ev_read(ue4_base + 0x2067c024, &initialized, 1) || initialized != 1 ||
        !ev_read(ue4_base + 0x2067c028, &tid, 4) || !tid || tid > 0x7fffffff) return 0;
    atomic_store(&ef_game_tid, (int)tid);
    return 1;
}
static int ef_hook(uint64_t object) {
    const uint32_t signature[] = {0xd101c3ff, 0xa9067bfd, 0x910183fd, 0x3942e428};
    uint32_t actual[4];
    if (!ev_read(ue4_base + EF_ACTOR_PE_RVA, actual, sizeof(actual)) ||
        memcmp(actual, signature, sizeof(actual))) return 0;
    uint64_t table = ev_ptr(object), slot = table + EF_PE_SLOT * 8, value = ev_ptr(slot);
    if (value == (uint64_t)(uintptr_t)ef_process_event) return 1;
    if (!is_in_ue4(table) || value != ue4_base + EF_ACTOR_PE_RVA) return 0;
    int protection = 0;
    for (int i = 0; i < num_regions; i++) if (slot >= regions[i].start && slot + 8 <= regions[i].end) {
        if (regions[i].perms[0] == 'r') protection |= PROT_READ;
        if (regions[i].perms[1] == 'w') protection |= PROT_WRITE;
        if (regions[i].perms[2] == 'x') protection |= PROT_EXEC;
        break;
    }
    if (!(protection & PROT_READ) || (protection & PROT_EXEC)) return 0;
    long size = sysconf(_SC_PAGESIZE);
    if (size <= 0) return 0;
    uintptr_t page = slot & ~((uintptr_t)size - 1);
    ef_original = (EVProcessEvent)(uintptr_t)(ue4_base + EF_ACTOR_PE_RVA);
    if (mprotect((void *)page, size, protection | PROT_WRITE)) return 0;
    uint64_t expected = value;
    int changed = __atomic_compare_exchange_n((uint64_t *)(uintptr_t)slot, &expected,
        (uint64_t)(uintptr_t)ef_process_event, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    int restored = mprotect((void *)page, size, protection) == 0;
    return changed && restored;
}
static int ef_bind(uint64_t pawn) {
    uint64_t cls = ev_ptr(pawn + 8);
    if (cls == ef_bound_class && ef_same_function(&ef_start) && ef_same_function(&ef_stop)) return 1;
    memset(&ef_start, 0, sizeof(ef_start)); memset(&ef_stop, 0, sizeof(ef_stop));
    for (uint64_t current = cls, n = 0; current && n < 32; n++, current = ev_ptr(current + 0x40)) {
        char name[80]; resolve_fname(read_i32(current + OFF_UOBJECT_NAME), name, sizeof(name));
        if (strcmp(name, "GPCharacterBase")) continue;
        for (uint64_t field = ev_ptr(current + 0x50), i = 0; field && i < 512; i++, field = ev_ptr(field + 0x28)) {
            resolve_fname(read_i32(field + OFF_UOBJECT_NAME), name, sizeof(name));
            int start = !strcmp(name, "StartFire"), stop = !strcmp(name, "StopFire");
            if ((!start && !stop) || ev_ptr(field + 0x10) != current) continue;
            uint16_t bytes = 0; uint32_t flags = 0;
            if (!ev_read(field + 0xb2, &bytes, 2) || bytes != 4 ||
                !ev_read(field + 0xb8, &flags, 4) || !(flags & 0x400) ||
                ev_ptr(field + 0xd8) != ue4_base + (start ? 0xc595a6c : 0xc595c18)) continue;
            ef_ref(field, start ? &ef_start : &ef_stop);
        }
        break;
    }
    ef_bound_class = cls;
    return ef_start.object.address && ef_stop.object.address;
}
static int ef_method(uint64_t object, const char *owner, const char *wanted,
                     uint16_t expected_size, uint64_t exec_rva, EFRef *out) {
    memset(out, 0, sizeof(*out));
    for (uint64_t cls = ev_ptr(object + 8), n = 0; cls && n < 32; n++, cls = ev_ptr(cls + 0x40)) {
        char name[80]; resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, sizeof(name));
        if (strcmp(name, owner)) continue;
        for (uint64_t field = ev_ptr(cls + 0x50), i = 0; field && i < 512; i++, field = ev_ptr(field + 0x28)) {
            resolve_fname(read_i32(field + OFF_UOBJECT_NAME), name, sizeof(name));
            if (strcmp(name, wanted) || ev_ptr(field + 0x10) != cls) continue;
            uint16_t size = 0; uint32_t flags = 0;
            return ev_read(field + 0xb2, &size, 2) && size == expected_size &&
                ev_read(field + 0xb8, &flags, 4) && (flags & 0x400) &&
                ev_ptr(field + 0xd8) == ue4_base + exec_rva && ef_ref(field, out);
        }
        break;
    }
    return 0;
}
static int ef_bind_zoom(EFCommand *c) {
    if (ef_zoom_weapon.object.address == c->weapon.object.address && ef_same(&ef_zoom_weapon)) {
        c->zoom = ef_zoom_bindings;
        if (ef_zoom_valid(c)) return 1;
    }
    EFZoomRefs *z = &c->zoom;
    uint64_t input = ev_ptr(c->controller.object.address + 0x960);
    uint64_t proxy = ew_find_component_in(c->weapon.object.address, "WeaponDataComponentProxy", 0x878, 0xac8);
    if (!ef_ref(input, &z->input) || !ef_ref(proxy, &z->proxy) ||
        !ef_method(input, "GPInputManager", "OnInputKeyAim", 0, 0xc5e816c, &z->aim) ||
        !ef_method(input, "GPInputManager", "OnInputKeyUnAim", 0, 0xc5e8604, &z->unaim) ||
        !ef_method(proxy, "WeaponDataComponentProxy", "GetZoomProgress", 4, 0xc6f424c, &z->progress) ||
        !ef_method(proxy, "GPWeaponDataComponentProxy", "IsZooming", 1, 0x9e9bd34, &z->is_on)) return 0;
    ef_zoom_weapon = c->weapon; ef_zoom_bindings = *z;
    return ef_zoom_valid(c);
}
static void ef_publish(int enabled, int fire, uint64_t pawn, uint64_t controller, uint64_t target) {
    EFCommand command = {.at = ef_now(), .enabled = enabled, .fire = fire,
        .auto_zoom = (atomic_load(&ea_config) & (1u << 25)) != 0};
    if (enabled && (!ef_bind(pawn) || !ef_ref(pawn, &command.pawn) ||
        !ef_ref(controller, &command.controller) ||
        !ef_ref(ev_ptr(ev_ptr(pawn + 0x3b18) + 0x218), &command.weapon))) command.enabled = 0;
    if (command.enabled) {
        command.start = ef_start; command.stop = ef_stop;
        if (command.auto_zoom && !ef_bind_zoom(&command)) command.enabled = 0;
        if (fire && !ef_ref(target, &command.target)) command.fire = 0;
        int hooked = 0;
        if (ef_bind_thread()) {
            hooked = ef_hook(controller);
            hooked |= ef_hook(pawn);
        }
        if (!hooked) command.enabled = 0;
    }
    command.error = enabled && !command.enabled;
    pthread_mutex_lock(&ef_mutex);
    ef_request = command;
    pthread_mutex_unlock(&ef_mutex);
    if (enabled && !command.enabled) atomic_store(&ef_status, 3);
}
#endif
