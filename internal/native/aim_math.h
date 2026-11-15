#ifndef INTERNAL_AIM_MATH_H
#define INTERNAL_AIM_MATH_H
#include <math.h>
#include <stdint.h>

/* The same angular radius drives projection-space selection and drawing. */
static float aim_radius_pixels(float width, float camera_fov, float radius_degrees) {
    if (!isfinite(width) || width <= 0 || !isfinite(camera_fov) || camera_fov <= 1 ||
        camera_fov >= 179 || !isfinite(radius_degrees) || radius_degrees <= 0 || radius_degrees >= 89) return 0;
    const float radians = 0.017453292519943295f;
    return width * 0.5f * tanf(radius_degrees * radians) / tanf(camera_fov * 0.5f * radians);
}

/* Squared radius in camera tangent space; behind-camera points are ineligible. */
static float aim_screen_distance(const float camera[3], float yaw, float pitch, const float point[3]) {
    const float radians = 0.017453292519943295f;
    float y = yaw * radians, p = pitch * radians;
    float dx = point[0]-camera[0], dy = point[1]-camera[1], dz = point[2]-camera[2];
    float forward = dx*cosf(p)*cosf(y) + dy*cosf(p)*sinf(y) + dz*sinf(p);
    float right = -dx*sinf(y) + dy*cosf(y);
    float up = -dx*sinf(p)*cosf(y) - dy*sinf(p)*sinf(y) + dz*cosf(p);
    if (!isfinite(forward) || !isfinite(right) || !isfinite(up) || forward < 0.5f) return INFINITY;
    return (right*right + up*up)/(forward*forward);
}
static int aim_inside(float distance_squared, float degrees) {
    if (!isfinite(degrees) || degrees <= 0 || degrees >= 89 || !isfinite(distance_squared)) return 0;
    float radius = tanf(degrees * 0.017453292519943295f);
    return distance_squared <= radius*radius;
}
static int aim_rotation(const float camera[3], const float target[3], float rotation[3]) {
    float dx=target[0]-camera[0], dy=target[1]-camera[1], dz=target[2]-camera[2];
    float horizontal=hypotf(dx,dy);
    if (!isfinite(horizontal) || !isfinite(dz) || hypotf(horizontal,dz) < 0.5f) return 0;
    rotation[0]=atan2f(dz,horizontal)*57.29577951308232f;
    rotation[1]=atan2f(dy,dx)*57.29577951308232f;
    rotation[2]=0;
    return isfinite(rotation[0]) && isfinite(rotation[1]);
}

typedef struct { uint64_t actor; float distance_squared; int eligible; } AimChoice;
static int aim_select(const AimChoice *choices, int count, uint64_t locked_actor, float radius) {
    int selected=-1; float nearest=INFINITY;
    for (int i=0;i<count;i++) {
        if (!choices[i].eligible || !choices[i].actor || !isfinite(choices[i].distance_squared)) continue;
        if (locked_actor && choices[i].actor==locked_actor) return i;
        if (aim_inside(choices[i].distance_squared,radius) && choices[i].distance_squared<nearest) {
            selected=i; nearest=choices[i].distance_squared;
        }
    }
    return selected;
}
#endif
