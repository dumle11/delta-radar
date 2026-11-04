#ifndef DELTA_SKELETON_READER_H
#define DELTA_SKELETON_READER_H

/* Captured native GetBoneTransform chooses 750 + 16 * int32(mesh+794).
 * GetBoneName/GetNumBones use asset+1c8, stride 12, count asset+1d0.
 * These are component-space animated poses, not reference/local poses. */
typedef struct { uint64_t data; int32_t count, capacity; } SkeletonArray;
typedef struct {
    uint64_t mesh, root, asset, parent;
    SkeletonArray pose, info;
    uint32_t buffer;
    unsigned char relative[16];
    SkeletonTransform root_world, mesh_world;
} SkeletonState;
typedef struct {
    int pid;
    uint64_t asset, info, class_address;
    int32_t count, object_name;
    int16_t parents[MAX_SKELETON_BONES];
} SkeletonHierarchy;
static SkeletonHierarchy skeleton_hierarchies[16];
static unsigned skeleton_next_cache;

static int skeleton_profile_supported(void) {
    static uint64_t checked_base;
    static int supported;
    if (checked_base == ue4_base && supported) return 1;
    const uint32_t expected[] = {0xf9438c08, 0xb4000068, 0xb941d100, 0xd65f03c0};
    uint32_t actual[4];
    if (!ue4_base || read_mem(ue4_base + 0x1acda0fc, actual, sizeof(actual)) != sizeof(actual) ||
        memcmp(actual, expected, sizeof(expected))) return 0;
    checked_base = ue4_base; supported = 1; return 1;
}
static int skeleton_array_valid(const SkeletonArray *a) {
    return is_any_ptr(a->data) && a->count >= 2 && a->count <= MAX_SKELETON_BONES &&
        a->capacity >= a->count && a->capacity <= 4096;
}
static int skeleton_read_state(uint64_t actor, uint64_t root, SkeletonState *s) {
    unsigned char metadata[0x690];
    memset(s, 0, sizeof(*s));
    s->root = root; s->mesh = read_ptr(actor + 0x3d0);
    if (!is_any_ptr(s->mesh) || read_mem(s->mesh + 0x108, metadata, sizeof(metadata)) != sizeof(metadata)) return 0;
    s->parent = position_u64(metadata);
    /* Primary character mesh must attach directly to this actor's capsule.
     * Master-pose meshes need their own index remapping and are not admitted. */
    if (s->parent != root || position_u32(metadata + 0x640) != 0) return 0;
    s->asset = position_u64(metadata + 0x610);
    s->buffer = position_u32(metadata + 0x68c);
    if (!is_any_ptr(s->asset) || s->buffer > 1) return 0;
    memcpy(&s->pose, metadata + 0x648 + s->buffer * 16, sizeof(s->pose));
    memcpy(s->relative, metadata + 0x60, sizeof(s->relative));
    memcpy(&s->mesh_world, metadata + 0x108, sizeof(s->mesh_world));
    if (!enc_handler_unmanaged(s->relative + 12) || !skeleton_array_valid(&s->pose) ||
        read_mem(s->asset + 0x1c8, &s->info, sizeof(s->info)) != sizeof(s->info) ||
        !skeleton_array_valid(&s->info) || s->info.count != s->pose.count ||
        read_mem(root + 0x210, &s->root_world, sizeof(s->root_world)) != sizeof(s->root_world)) return 0;
    if (!enc_handler_unmanaged(s->root_world.padding) &&
        !position_component_orientation(actor, root, s->root_world.rotation, s->root_world.scale)) return 0;
    if (!enc_handler_unmanaged(s->mesh_world.padding) &&
        !position_component_orientation(actor, s->mesh, s->mesh_world.rotation, s->mesh_world.scale)) return 0;
    /* Translation is supplied by the verified position decoder, never the
     * potentially managed transform's raw translation. Clear padding/tags. */
    memset(s->root_world.translation, 0, sizeof(s->root_world.translation));
    memset(s->mesh_world.translation, 0, sizeof(s->mesh_world.translation));
    memset(s->root_world.padding, 0, sizeof(s->root_world.padding));
    memset(s->mesh_world.padding, 0, sizeof(s->mesh_world.padding));
    if (!skeleton_transform_valid(&s->root_world) || !skeleton_transform_valid(&s->mesh_world)) return 0;
    for (int j = 0; j < 3; j++) {
        float relative; memcpy(&relative, s->relative + j * 4, 4);
        if (!isfinite(relative) || fabsf(relative) > 300 ||
            fabsf(s->root_world.scale[j]) < 0.01f || fabsf(s->mesh_world.scale[j]) < 0.01f) return 0;
    }
    return 1;
}
static const SkeletonHierarchy *skeleton_hierarchy(const SkeletonState *s) {
    uint64_t cls = read_ptr(s->asset + 8);
    int32_t name = read_i32(s->asset + OFF_UOBJECT_NAME);
    if (!is_any_ptr(cls)) return NULL;
    for (unsigned i = 0; i < 16; i++) {
        SkeletonHierarchy *c = &skeleton_hierarchies[i];
        if (c->pid == game_pid && c->asset == s->asset && c->info == s->info.data &&
            c->count == s->info.count && c->class_address == cls && c->object_name == name) return c;
    }
    unsigned char info[MAX_SKELETON_BONES * 12];
    size_t bytes = (size_t)s->info.count * 12;
    if (read_mem(s->info.data, info, bytes) != (ssize_t)bytes) return NULL;
    SkeletonHierarchy candidate = {.pid = game_pid, .asset = s->asset, .info = s->info.data,
        .class_address = cls, .count = s->info.count, .object_name = name};
    for (int i = 0; i < s->info.count; i++) {
        int32_t parent; memcpy(&parent, info + i * 12 + 8, 4);
        if ((i == 0 && parent != -1) || (i > 0 && (parent < 0 || parent >= i))) return NULL;
        candidate.parents[i] = (int16_t)parent;
    }
    SkeletonHierarchy *c = &skeleton_hierarchies[skeleton_next_cache++ % 16];
    *c = candidate; return c;
}
static void read_entity_bones(Entity *e, uint64_t actor, uint64_t root) {
    e->has_bones = 0; e->bone_count = 0;
    if (!e->position_plaintext || e->dead || !skeleton_profile_supported()) return;
    for (unsigned attempt = 0; attempt < 2; attempt++) {
        SkeletonState state, after;
        SkeletonTransform pose[MAX_SKELETON_BONES], verify[MAX_SKELETON_BONES];
        if (!skeleton_read_state(actor, root, &state)) return;
        const SkeletonHierarchy *hierarchy = skeleton_hierarchy(&state);
        size_t bytes = (size_t)state.pose.count * sizeof(*pose);
        if (!hierarchy || read_mem(state.pose.data, pose, bytes) != (ssize_t)bytes) return;
        SkeletonTransform parent = state.root_world, world = state.mesh_world;
        float relative[3]; memcpy(relative, state.relative, sizeof(relative));
        parent.translation[0] = e->x * 100;
        parent.translation[1] = e->y * 100;
        parent.translation[2] = e->z * 100;
        skeleton_transform_point(&parent, relative, world.translation);
        for (int i = 0; i < state.pose.count; i++) {
            if (!skeleton_transform_valid(&pose[i])) return;
            for (int j = 0; j < 3; j++) if (fabsf(pose[i].translation[j]) > 2000) return;
            float point[3]; skeleton_transform_point(&world, pose[i].translation, point);
            for (int j = 0; j < 3; j++) {
                if (!isfinite(point[j]) || fabsf(point[j]) > 10000000) return;
                e->bone_pos[i][j] = point[j] / 100;
            }
            e->bone_parent[i] = hierarchy->parents[i];
        }
        if (read_mem(state.pose.data, verify, bytes) != (ssize_t)bytes ||
            memcmp(pose, verify, bytes) || !skeleton_read_state(actor, root, &after) ||
            memcmp(&state, &after, sizeof(state))) continue;
        e->bone_count = state.pose.count; e->has_bones = 1; return;
    }
}
#endif
