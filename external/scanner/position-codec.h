#ifndef DELTA_POSITION_CODEC_H
#define DELTA_POSITION_CODEC_H
#include <stdint.h>
#include <string.h>

/* ARM64 software path recovered from the captured 1.202 bounds getter.
 * Arithmetic wraps at the width of the corresponding W/X register. */
static inline uint64_t position_encode64(uint64_t x, uint64_t salt) {
    return (x + UINT64_C(0xcc6c1b23af0f4f80) + salt * UINT64_C(0xd873ca3a21fed0c2)) *
        UINT64_C(0x62f5ed5fe3634ff3) + salt * salt + salt + UINT64_C(0x40fb51c99606cb85);
}
static inline uint64_t position_decode64(uint64_t x, uint64_t salt) {
    return (x + UINT64_C(0xbf04ae3669f9347b) + (~salt) * salt) *
        UINT64_C(0x2f94e0e203c8e13b) + salt * UINT64_C(0x278c35c5de012f3e) +
        UINT64_C(0x3393e4dc50f0b080);
}
static inline uint32_t position_u32(const void *p) { uint32_t x; memcpy(&x, p, 4); return x; }
static inline uint64_t position_u64(const void *p) { uint64_t x; memcpy(&x, p, 8); return x; }

typedef struct {
    uint64_t tls, gpu;
    uint32_t salt, shift, count_term, counter_term, encoded_term, slot_bias, mask;
} PositionCodec;

static inline unsigned position_vector_slot(const PositionCodec *c, uint32_t token) {
    uint32_t x = c->counter_term + c->count_term + (uint32_t)((c->gpu >> (c->shift & 63)) & 15) +
        (uint32_t)position_decode64(c->encoded_term, c->salt);
    x *= UINT32_C(0x9e3779b1);
    x ^= x >> 16;
    x *= UINT32_C(0x85ebca6b);
    x ^= x >> 13;
    uint32_t factor = x + UINT32_C(0x9e3779b1) + 8;
    x += c->slot_bias + (uint32_t)position_decode64(token, (uint32_t)c->tls);
    x *= factor;
    x ^= x >> 16;
    return x % 10;
}
static inline void position_vector_decode(const PositionCodec *c, const unsigned char input[12], float out[3]) {
    uint32_t z = position_u32(input + 8) ^ (c->mask & (uint32_t)c->tls);
    uint64_t yz = position_decode64(position_u32(input + 4) | ((uint64_t)z << 32), c->tls);
    uint64_t xy = position_decode64(position_u32(input) | (yz << 32), c->tls);
    uint32_t result[3] = {(uint32_t)xy, (uint32_t)(xy >> 32), (uint32_t)(yz >> 32)};
    memcpy(out, result, sizeof(result));
}
#endif
