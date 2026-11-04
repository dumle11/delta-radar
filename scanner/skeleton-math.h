#ifndef DELTA_SKELETON_MATH_H
#define DELTA_SKELETON_MATH_H
#include <stdint.h>
#include <math.h>
#define MAX_SKELETON_BONES 512

/* This build packs FVector fields: scale starts at 28, not 32. */
typedef struct {
    float rotation[4], translation[3], scale[3];
    uint8_t padding[8];
} SkeletonTransform;
_Static_assert(sizeof(SkeletonTransform) == 48, "captured ARM64 transform layout");

static inline int skeleton_transform_valid(const SkeletonTransform *t) {
    float norm = 0;
    for (int j = 0; j < 4; j++) {
        if (!isfinite(t->rotation[j])) return 0;
        norm += t->rotation[j] * t->rotation[j];
    }
    for (int j = 0; j < 3; j++)
        if (!isfinite(t->translation[j]) || !isfinite(t->scale[j]) ||
            fabsf(t->scale[j]) > 10) return 0;
    return norm > 0.98f && norm < 1.02f;
}
static inline void skeleton_transform_point(const SkeletonTransform *t, const float p[3], float out[3]) {
    float v[3], cross[3], twice[3];
    for (int j = 0; j < 3; j++) v[j] = p[j] * t->scale[j];
    cross[0] = t->rotation[1] * v[2] - t->rotation[2] * v[1];
    cross[1] = t->rotation[2] * v[0] - t->rotation[0] * v[2];
    cross[2] = t->rotation[0] * v[1] - t->rotation[1] * v[0];
    for (int j = 0; j < 3; j++) twice[j] = 2 * cross[j];
    out[0] = t->translation[0] + v[0] + t->rotation[3] * twice[0] + t->rotation[1] * twice[2] - t->rotation[2] * twice[1];
    out[1] = t->translation[1] + v[1] + t->rotation[3] * twice[1] + t->rotation[2] * twice[0] - t->rotation[0] * twice[2];
    out[2] = t->translation[2] + v[2] + t->rotation[3] * twice[2] + t->rotation[0] * twice[1] - t->rotation[1] * twice[0];
}
#endif
