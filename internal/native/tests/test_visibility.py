"""Exercise production bounds rays with synthetic scene geometry."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[3] / "external/scanner/memscan-legacy.c").read_text()
start = source.index("static int ray_hits_aabb(")
end = source.index("#define OFF_COMPONENT_BOUNDS", start)
program = r"""
#include <assert.h>
#include <math.h>
#include <stdint.h>
typedef struct { float min[3], max[3]; uint64_t actor_addr, owner_addr; } OccluderAABB;
static OccluderAABB occluders[8];
static int num_occluders, occlusion_incomplete;
#include <string.h>
#include <stdio.h>
#define MAX_OCCLUDERS 8
#define OFF_UOBJECT_CLASS 0x8
#define OFF_UOBJECT_NAME 0x1c
#define OFF_COMPONENT_BOUNDS 0x148
static unsigned char heap[16384];
static uint64_t verified_uworld = 1;
static int occ_dbg_calls, occ_dbg_no_root, occ_dbg_no_bounds;
static int occ_dbg_no_mesh, occ_dbg_no_pos;
static int occ_dbg_too_small, occ_dbg_too_far, occ_dbg_added;
static int is_telemetry_ptr(uint64_t p) {
    return p >= (uint64_t)heap && p < (uint64_t)(heap + sizeof(heap));
}
static int is_any_ptr(uint64_t p) { return is_telemetry_ptr(p); }
static long read_mem(uint64_t p, void *out, size_t n) {
    if (!is_telemetry_ptr(p) || n > (uint64_t)(heap + sizeof(heap)) - p) return -1;
    memcpy(out, (void *)p, n); return (long)n;
}
static uint64_t read_ptr(uint64_t p) { uint64_t v = 0; read_mem(p, &v, 8); return v; }
static int32_t read_i32(uint64_t p) { int32_t v = 0; read_mem(p, &v, 4); return v; }
static void resolve_fname(int index, char *out, size_t n) {
    snprintf(out, n, "%s", index == 1 ? "PrimitiveComponent" :
        index == 2 ? "SceneComponent" : "StaticMeshComponent");
}
static int decode_enabled;
static int enc_handler_unmanaged(const unsigned char *p) { return p[0] == 255 && p[1] == 255 && p[2] == 0; }
static int position_decode_component(uint64_t component, float out[3]) {
    (void)component;
    if (!decode_enabled) return 0;
    out[0] = 500; out[1] = out[2] = 0; return 1;
}
static void put_ptr(uint64_t p, uint64_t v) { memcpy((void *)p, &v, 8); }
static void put_int(uint64_t p, int v) { memcpy((void *)p, &v, 4); }
""" + source[start:end] + source[
    source.index("static uint64_t occ_cur_actor = 0;"):
    source.index("static uint64_t cached_local_class = 0;")
] + r"""
int main(void) {
    float origin[3] = {0,0,0}, parallel[3] = {1,0,0};
    OccluderAABB wall = {{4,-0.3f,-2},{6,0.3f,2},0};
    assert(ray_hits_aabb(origin, parallel, &wall, 10));
    assert(!ray_hits_aabb(origin, parallel, &wall, 3));
    origin[1] = 1;
    assert(!ray_hits_aabb(origin, parallel, &wall, 10));
    origin[1] = wall.max[1];
    assert(ray_hits_aabb(origin, parallel, &wall, 10));
    assert(-1 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 0));
    assert(-1 == is_visible_from_camera(NAN,0,0, 10,0,0, 1.05f, 0));
    occluders[0] = wall; num_occluders = 1;
    /* The former 1–2m side rays incorrectly escaped this body-width wall. */
    assert(0 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 0));
    occluders[0].max[2] = 0.2f;
    assert(1 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 0));
    /* A close obstacle and an obstacle in the last 5% both block the ray. */
    occluders[0] = (OccluderAABB){{0.5f,-2,-2},{1,2,2},0};
    assert(0 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 0));
    occluders[0] = (OccluderAABB){{9.7f,-2,-2},{9.9f,2,2},0};
    assert(0 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 0));
    occluders[0] = (OccluderAABB){{11,-2,-2},{12,2,2},0};
    assert(1 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 0));
    occlusion_incomplete = 1;
    assert(-1 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 0));
    occlusion_incomplete = 0;
    /* Camera-containing coarse bounds cannot prove clear line of sight. */
    occluders[0] = (OccluderAABB){{-1,-2,-2},{1,2,2},0};
    assert(-1 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 0));
    /* A target inside bounds is still blocked by the entry surface. */
    occluders[0] = (OccluderAABB){{8,-2,-2},{12,2,2},0};
    assert(0 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 0));
    occluders[0].owner_addr = 1234;
    occlusion_incomplete = 0;
    assert(1 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 1234));
    assert(0 == is_visible_from_camera(0,0,0, 10,0,0, 1.05f, 9999));
    /* Collector fixture: a plain scene root with an occluding mesh child. */
    uint64_t primitive = (uint64_t)heap + 1024, scene = (uint64_t)heap + 2048;
    uint64_t mesh = (uint64_t)heap + 3072, root = (uint64_t)heap + 4096;
    uint64_t child = (uint64_t)heap + 5120, actor = (uint64_t)heap + 6144;
    uint64_t children = (uint64_t)heap + 7168;
    put_int(primitive + OFF_UOBJECT_NAME, 1); put_int(scene + OFF_UOBJECT_NAME, 2);
    put_int(primitive + 0x24, 0xC3); put_int(scene + 0x24, 0xC2);
    put_ptr(primitive + 0x40, scene);
    put_int(mesh + OFF_UOBJECT_NAME, 3); put_ptr(mesh + 0x40, primitive);
    put_ptr(root + OFF_UOBJECT_CLASS, scene); put_ptr(child + OFF_UOBJECT_CLASS, mesh);
    put_ptr(actor + 0x100, root); put_ptr(root + 0x118, children);
    put_ptr(actor + 0x120, 99);
    put_int(root + 0x120, 1); put_ptr(children, child);
    float bounds[8] = {500,0,0,0, 50,200,200,300};
    memcpy((void *)(child + OFF_COMPONENT_BOUNDS), bounds, sizeof(bounds));
    unsigned char *tag = (unsigned char *)(child + OFF_COMPONENT_BOUNDS + 12);
    tag[0] = tag[1] = 255;
    *(unsigned char *)(child + 0x348) = 1; /* QueryOnly */
    *(unsigned char *)(child + 0x373) = 2; /* Visibility: Block */
    /* GPU occluder hints are clear; query-blocking cover still participates. */
    num_occluders = occlusion_incomplete = 0;
    try_add_occluder(actor, 0x100, 0, 0,0,0);
    assert(num_occluders == 1 && occluders[0].actor_addr == actor && occluders[0].owner_addr == 99);
    assert(fabsf(occluders[0].min[0] - 4.5f) < 0.001f);
    assert(fabsf(occluders[0].max[1] - 2.0f) < 0.001f);
    *(unsigned char *)(child + 0x19c) = 0x60; /* hidden collision remains relevant */
    num_occluders = 0;
    try_add_occluder(actor, 0x100, 0, 0,0,0);
    assert(num_occluders == 1);
    *(unsigned char *)(child + 0x348) = 2; /* PhysicsOnly has no queries */
    num_occluders = 0;
    try_add_occluder(actor, 0x100, 0, 0,0,0);
    assert(num_occluders == 0);
    *(unsigned char *)(child + 0x348) = 3;
    *(unsigned char *)(child + 0x373) = 0; /* ignores visibility */
    try_add_occluder(actor, 0x100, 0, 0,0,0);
    assert(num_occluders == 0);
    *(unsigned char *)(child + 0x373) = 2;
    /* Thin walls and large walls must not be discarded. */
    float thin = 2, large = 10000;
    memcpy((void *)(child + 0x158), &thin, 4);
    memcpy((void *)(child + 0x15c), &large, 4);
    try_add_occluder(actor, 0x100, 0, 0,0,0);
    assert(num_occluders == 1);
    /* Managed coordinates require the checked decoder, never raw ciphertext. */
    tag[0] = tag[1] = 0;
    num_occluders = occlusion_incomplete = 0;
    try_add_occluder(actor, 0x100, 0, 0,0,0);
    assert(num_occluders == 0 && occlusion_incomplete);
    decode_enabled = 1; occlusion_incomplete = 0;
    try_add_occluder(actor, 0x100, 0, 0,0,0);
    assert(num_occluders == 1 && !occlusion_incomplete);
    assert(fabsf(occluders[0].min[0] - 4.98f) < 0.001f);
    return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    cfile = Path(directory) / "visibility.c"
    executable = Path(directory) / "visibility"
    cfile.write_text(program)
    subprocess.run(["cc", "-O2", str(cfile), "-o", str(executable), "-lm"], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: bounds rays, partial/near/endpoint cover, query-blocking cover, thin/large walls, managed bounds, unknown visibility")
