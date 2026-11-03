#include "../position-codec.h"
#include <assert.h>
#include <stdio.h>

static void test_roundtrip(void) {
    uint64_t random = 20261103;
    for (unsigned i = 0; i < 10000; i++) {
        random = random * UINT64_C(6364136223846793005) + 1;
        uint64_t salt = random, value = random ^ (random >> 17);
        assert(position_decode64(position_encode64(value, salt), salt) == value);
        PositionCodec c = {.tls = salt, .mask = (uint32_t)(salt >> 32)};
        uint32_t expected[3] = {(uint32_t)value, (uint32_t)(value >> 32), (uint32_t)(salt >> 13)};
        uint64_t xy = position_encode64(expected[0] | ((uint64_t)expected[1] << 32), salt);
        uint64_t yz = position_encode64((xy >> 32) | ((uint64_t)expected[2] << 32), salt);
        uint32_t encoded[3] = {(uint32_t)xy, (uint32_t)yz, (uint32_t)(yz >> 32) ^ (c.mask & (uint32_t)salt)};
        float decoded[3];
        position_vector_decode(&c, (const unsigned char *)encoded, decoded);
        assert(memcmp(decoded, expected, sizeof(expected)) == 0);
    }
}

/* Expected slots and outputs come from isolated execution of the captured ARM
 * instructions, independently of this C port. No process addresses are used. */
static void test_captured_arm_vectors(void) {
    PositionCodec c = {.tls = UINT64_C(0x5003c5000), .gpu = UINT64_C(0x5003c6000),
        .salt = UINT32_C(0x9d96f200), .shift = 12, .counter_term = 31,
        .encoded_term = UINT32_C(4010570615), .slot_bias = 7, .mask = UINT32_MAX};
    const uint32_t tokens[4] = {0xa29fc63f, 0xb0a146f7, 0x97cc95bd, 0x7d385a7d};
    const unsigned slots[4] = {4, 8, 5, 9};
    const uint32_t input[4][3] = {
        {0xb336d085, 0xda450674, 0x0878e230},
        {0xb336d085, 0xda450674, 0xe48ae230},
        {0x7d100e05, 0x960435c8, 0xf95e65ce},
        {0x7d100e05, 0x960435c8, 0xf95e65ce}
    };
    const float expected[4][3] = {
        {-4028.46875f, -12775.1875f, -155.654541015625f},
        {-4028.46875f, -12775.1875f, -225.654541015625f},
        {-4942.5f, -12494.6875f, -98.91015625f},
        {-4942.5f, -12494.6875f, -98.91015625f}
    };
    for (unsigned i = 0; i < 4; i++) {
        assert(position_vector_slot(&c, tokens[i]) == slots[i]);
        float result[3];
        position_vector_decode(&c, (const unsigned char *)input[i], result);
        assert(memcmp(result, expected[i], sizeof(result)) == 0);
    }
}
int main(void) {
    test_roundtrip(); test_captured_arm_vectors();
    puts("position codec: 10000 roundtrips and 4 independent ARM fixtures passed");
    return 0;
}
