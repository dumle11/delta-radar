#ifndef DELTA_POSITION_DECODER_H
#define DELTA_POSITION_DECODER_H
#include "position-codec.h"

/* External, read-only resolver for the captured Proxima 1.202 ARM64 software
 * decoder. Unknown accessor, libc, hardware path, or changing records fail closed.
 * No target calls, hooks, proc-mem access, or GPU ioctls are performed here. */
typedef struct {
    int pid, ready;
    uint64_t base, context, object, thread, libc, refresh_ms;
    uint32_t key, tid;
} PositionDecoder;
static PositionDecoder position_decoder;

static int position_read(uint64_t a, void *p, size_t n) {
    return a >= UINT64_C(0x100000000) && a < UINT64_C(0x8000000000) &&
        n <= 16384 && a + n > a && read_mem(a, p, n) == (ssize_t)n;
}
static int position_pointer(uint64_t a, uint64_t *p) {
    return position_read(a, p, 8) && *p >= UINT64_C(0x100000000) && *p < UINT64_C(0x8000000000);
}
static int position_provider_object(uint64_t primary, uint64_t *object) {
    uint64_t a, b;
    return position_pointer(primary + 0x10, &a) && position_pointer(a + 0x10, &b) &&
        position_pointer(b, object);
}
static uint64_t position_now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static int position_decoder_discover(void) {
    PositionDecoder *d = &position_decoder;
    uint64_t now = position_now_ms();
    if (d->pid == game_pid && d->base == ue4_base && now - d->refresh_ms < 1000) return d->ready;
    memset(d, 0, sizeof(*d)); d->pid = game_pid; d->base = ue4_base; d->refresh_ms = now;
    if (!use_vm_readv || !ue4_base) return 0;
    unsigned char entry[32], trampoline[168], prefix[16], object[160];
    const uint32_t accessor[] = {0x91084000, 0x58000050, 0xd61f0200};
    const uint32_t callback[] = {0x14000002, 0xd65f03c0, 0x14000001, 0xa9ba7bfd};
    const uint32_t vector[] = {0xd10283ff, 0xf90023fe, 0xa9056ffc, 0xa90667fa};
    if (!position_read(ue4_base + 0x107bbb8c, entry, sizeof(entry)) ||
        memcmp(entry, accessor, sizeof(accessor))) return 0;
    uint64_t t = position_u64(entry + 12), code, primary, vector_primary, vector_object;
    if (!position_read(t, trampoline, sizeof(trampoline)) ||
        position_u32(trampoline + 0x8c) != 0x58fffb60) return 0;
    code = position_u64(trampoline + 0xa0);
    if (!position_read(code, prefix, sizeof(prefix)) || memcmp(prefix, callback, sizeof(callback)) ||
        !position_read(code + 0x5e150, prefix, sizeof(prefix)) || memcmp(prefix, vector, sizeof(vector)) ||
        !position_pointer(t - 8, &primary) || !position_provider_object(primary, &d->object) ||
        !position_pointer(code + 0x5ec28, &vector_primary) ||
        !position_provider_object(vector_primary, &vector_object) || vector_object != d->object ||
        !position_read(d->object, object, sizeof(object)) || position_u32(object + 0x94) != 1) return 0;
    d->context = position_decode64(position_u64(object + 8), d->object ^ UINT64_C(0xd6f0bce));
    d->key = position_u32(object + 0x90);
    if ((d->key & UINT32_C(0x7fffffff)) >= 130 ||
        !position_read(d->context + 0x28cf0, &d->tid, 4) || !d->tid) return 0;
    for (int i = 0; i < num_regions; i++)
        if (strstr(regions[i].name, "/bionic/libc.so") && (!d->libc || regions[i].start < d->libc))
            d->libc = regions[i].start;
    /* Large matches can fill the scanner's general VMA inventory before libc.
     * Resolve this one named module from map metadata without that array cap. */
    if (!d->libc) {
        char path[64], line[1024];
        snprintf(path, sizeof(path), "/proc/%d/maps", game_pid);
        FILE *f = fopen(path, "r");
        if (f) {
            for (unsigned lines = 0; lines < 65536 && fgets(line, sizeof(line), f); lines++) {
                unsigned long long start;
                if (strstr(line, "/bionic/libc.so") && sscanf(line, "%llx-", &start) == 1 &&
                    (!d->libc || start < d->libc)) d->libc = start;
            }
            fclose(f);
        }
    }
    /* Pin the bionic thread/key layout to its readable ELF build ID. Its
     * getspecific code page is execute-only and must never be probed. */
    const unsigned char libc_build_id[] = {
        0xe5, 0x5e, 0x6e, 0x4c, 0x63, 0x15, 0x09, 0x59,
        0x86, 0x33, 0x76, 0x97, 0x98, 0x68, 0x30, 0x23
    };
    if (!d->libc || position_u64(object + 0x58) != d->libc + 0xe7070 ||
        !position_read(d->libc + 0x1b478, prefix, sizeof(prefix)) ||
        memcmp(prefix, libc_build_id, sizeof(libc_build_id))) return 0;
    uint64_t thread;
    if (!position_pointer(d->libc + 0xf75f8, &thread)) return 0;
    for (unsigned i = 0; thread && i < 512; i++) {
        unsigned char header[24];
        if (!position_read(thread, header, sizeof(header))) return 0;
        if (position_u32(header + 16) == d->tid) { d->thread = thread; d->ready = 1; return 1; }
        uint64_t next = position_u64(header);
        if (next == thread) return 0;
        thread = next;
    }
    return 0;
}

typedef struct {
    uint64_t table, sorted;
    uint32_t count;
    PositionCodec codec;
} PositionVectorState;
static int position_vector_state(PositionVectorState *s) {
    PositionDecoder *d = &position_decoder;
    unsigned char a[576], b[3032], object[160]; uint64_t gpu, tls, generation, entry[2]; uint32_t tid;
    if (!position_read(d->context + 0x2a1d0, a, sizeof(a)) ||
        !position_read(d->context + 0x2bae8, b, sizeof(b)) ||
        !position_read(d->object, object, sizeof(object)) || position_u32(object + 0x94) != 1 ||
        position_u32(object + 0x90) != d->key ||
        !position_read(d->thread + 16, &tid, 4) || tid != d->tid ||
        !position_pointer(d->thread + 0x2b0, &tls) ||
        !position_read(d->libc + 0xf8350 + (d->key & UINT32_C(0x7fffffff)) * 16, &generation, 8) ||
        !position_read(tls + (d->key & UINT32_C(0x7fffffff)) * 16, entry, sizeof(entry)) ||
        !(generation & 1) || entry[0] != generation || !entry[1] ||
        !position_read(d->context + 0x9d0, &gpu, 8) || !gpu) return 0;
    memset(s, 0, sizeof(*s));
    s->codec.salt = position_u32(b + 0x954);
    uint64_t mode = position_decode64(position_u64(a + 0x28), s->codec.salt);
    if ((mode != 1 && mode != 6) || a[0] != 0) return 0; /* PACGA path unsupported */
    uint32_t base_salt;
    uint64_t encoded;
    if (!position_read(d->context + 0x28ce0, &base_salt, 4) ||
        !position_read(d->context + 0x20 + 45 * 4, &s->count, 4) || !s->count || s->count > 4096 ||
        !position_read(d->context + 0x29548 + 45 * 8, &encoded, 8) ||
        !position_pointer(d->context + 0x2a470 + 45 * 8, &s->sorted) ||
        !position_read(d->context + 0xa34, &s->codec.shift, 4)) return 0;
    s->table = position_decode64(encoded, (uint32_t)(base_salt + 45));
    if (s->table < UINT64_C(0x100000000) || s->table >= UINT64_C(0x8000000000)) return 0;
    s->codec.tls = entry[1]; s->codec.gpu = gpu;
    s->codec.count_term = (int32_t)position_u32(a + 0x30) >= 0 ? position_u32(a + 0x6c) : 0;
    s->codec.counter_term = position_u32(b + 0x2b8);
    s->codec.encoded_term = position_u32(b + 0x298);
    s->codec.slot_bias = position_u32(a + 0x22c);
    s->codec.mask = position_u32(b + 0xbcc);
    return 1;
}
static int position_decode_component(uint64_t component, float out[3]) {
    if (!position_decoder_discover()) return 0;
    unsigned char tag[16], row[2400], after[2400], tag_after[16];
    PositionVectorState state, state_after;
    if (!position_read(component + 0x148, tag, sizeof(tag)) || enc_handler_unmanaged(tag + 12) ||
        !position_vector_state(&state)) return 0;
    uint64_t key = position_encode64(component + 0x154, state.codec.salt), address = 0;
    int lo = 0, hi = (int)state.count - 1;
    while (lo <= hi) {
        unsigned mid = (unsigned)(lo + (hi - lo) / 2); uint32_t id; uint64_t found;
        if (!position_read(state.sorted + mid * 4, &id, 4) || id > 65535) return 0;
        address = state.table + (uint64_t)id * 2400;
        if (!position_read(address + 0x910, &found, 8)) return 0;
        if (found == key) break;
        address = 0;
        if (found < key) lo = (int)mid + 1; else hi = (int)mid - 1;
    }
    if (!address || !position_read(address, row, sizeof(row)) || position_u64(row + 0x910) != key ||
        row[0x928] != 0) return 0;
    /* The native getter fills this optional source cache lazily (its +0x150
     * branch accepts zero). The encoded handler key is the record identity. */
    uint64_t source = position_u64(row + 0x918);
    if (source && source != component + 0x148) return 0;
    unsigned slot = position_vector_slot(&state.codec, position_u32(row + 0x938));
    const unsigned char *cipher = row + 8 + slot * 12;
    if (!position_u64(cipher) && !position_u32(cipher + 8)) return 0;
    position_vector_decode(&state.codec, cipher, out);
    if (!position_read(address, after, sizeof(after)) || memcmp(row, after, sizeof(row)) ||
        !position_read(component + 0x148, tag_after, sizeof(tag_after)) || memcmp(tag, tag_after, sizeof(tag)) ||
        !position_vector_state(&state_after) || memcmp(&state, &state_after, sizeof(state))) return 0;
    for (int i = 0; i < 3; i++) if (!isfinite(out[i]) || fabsf(out[i]) > 10000000) return 0;
    return out[0] != 0 || out[1] != 0 || out[2] != 0;
}
static int position_decode_actor(uint64_t actor, uint64_t root, float out[3]) {
    uint64_t mesh, root_after, mesh_after;
    float a[3], b[3];
    if (!position_pointer(actor + 0x3d0, &mesh) || mesh == root ||
        !position_decode_component(root, a) || !position_decode_component(mesh, b) ||
        !position_pointer(actor + 0x180, &root_after) || root_after != root ||
        !position_pointer(actor + 0x3d0, &mesh_after) || mesh_after != mesh) return 0;
    float dx = a[0] - b[0], dy = a[1] - b[1];
    if (dx * dx + dy * dy > 100 * 100 || fabsf(a[2] - b[2]) > 300) return 0;
    memcpy(out, a, sizeof(a)); return 1;
}

/* The captured transform redirect may select any of ten transform slots.
 * Its translation selector is deliberately not used here. Rotation and scale
 * are usable only when EVERY selectable slot contains identical bytes, so the
 * unknown selector cannot affect either value. */
static int position_component_orientation(uint64_t actor, uint64_t component,
                                          float rotation[4], float scale[3]) {
    if (!position_decoder_discover()) return 0;
    uint64_t ctx = position_decoder.context, encoded, sorted, table;
    uint32_t salt, base_salt, count;
    if (!position_read(ctx + 0x2c43c, &salt, 4) || !position_read(ctx + 0x28ce0, &base_salt, 4) ||
        !position_read(ctx + 0x20, &count, 4) || !count || count > 4096 ||
        !position_read(ctx + 0x29548, &encoded, 8) || !position_pointer(ctx + 0x2a470, &sorted)) return 0;
    table = position_decode64(encoded, base_salt);
    uint64_t key = position_encode64(component + 0x210, salt), address = 0;
    int lo = 0, hi = (int)count - 1;
    while (lo <= hi) {
        unsigned mid = (unsigned)(lo + (hi - lo) / 2); uint32_t id; uint64_t found;
        if (!position_read(sorted + mid * 4, &id, 4) || id > 65535) return 0;
        address = table + (uint64_t)id * 2400;
        if (!position_read(address + 0x910, &found, 8)) return 0;
        if (found == key) break;
        address = 0;
        if (found < key) lo = (int)mid + 1; else hi = (int)mid - 1;
    }
    unsigned char row[2400], after[2400];
    if (!address || !position_read(address, row, sizeof(row)) || position_u64(row + 0x910) != key ||
        position_decode64(position_u64(row + 0x920), salt) != actor || row[0x928]) return 0;
    for (unsigned i = 1; i < 10; i++)
        if (memcmp(row + 8, row + 8 + i * 48, 16) ||
            memcmp(row + 8 + 28, row + 8 + i * 48 + 28, 12)) return 0;
    if (!position_read(address, after, sizeof(after)) || memcmp(row, after, sizeof(row))) return 0;
    memcpy(rotation, row + 8, 16); memcpy(scale, row + 8 + 28, 12);
    return 1;
}
#endif
