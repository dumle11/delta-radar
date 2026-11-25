/* Draw ESP geometry (boxes, skeleton, snap lines, health bars) directly in the
 * present render pass using the exact submitted skeletal draw matrix and an immutable scene snapshot. */
#ifndef INTERNAL_ESP_GPU_H
#define INTERNAL_ESP_GPU_H
#include "entity_presentation.h"
#include <vulkan/vulkan.h>
#include <math.h>
#include "loot_filter.h"

/* SPIR-V shaders: vertex (pos vec2 + color vec4 -> NDC) and passthrough frag */
static const uint32_t eg_vert_spv[]={
    0x07230203,0x00010000,0x0008000b,0x00000032,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0009000f,0x00000000,0x00000004,0x6e69616d,0x00000000,0x0000000d,0x00000012,0x0000002e,
    0x00000030,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,0x00000000,
    0x00060005,0x0000000b,0x505f6c67,0x65567265,0x78657472,0x00000000,0x00060006,0x0000000b,
    0x00000000,0x505f6c67,0x7469736f,0x006e6f69,0x00070006,0x0000000b,0x00000001,0x505f6c67,
    0x746e696f,0x657a6953,0x00000000,0x00070006,0x0000000b,0x00000002,0x435f6c67,0x4470696c,
    0x61747369,0x0065636e,0x00070006,0x0000000b,0x00000003,0x435f6c67,0x446c6c75,0x61747369,
    0x0065636e,0x00030005,0x0000000d,0x00000000,0x00030005,0x00000012,0x00736f70,0x00030005,
    0x00000017,0x00004350,0x00040006,0x00000017,0x00000000,0x00007773,0x00040006,0x00000017,
    0x00000001,0x00006873,0x00030005,0x00000019,0x00000000,0x00040005,0x0000002e,0x6c6f6376,
    0x0000726f,0x00040005,0x00000030,0x6f6c6f63,0x00000072,0x00030047,0x0000000b,0x00000002,
    0x00050048,0x0000000b,0x00000000,0x0000000b,0x00000000,0x00050048,0x0000000b,0x00000001,
    0x0000000b,0x00000001,0x00050048,0x0000000b,0x00000002,0x0000000b,0x00000003,0x00050048,
    0x0000000b,0x00000003,0x0000000b,0x00000004,0x00040047,0x00000012,0x0000001e,0x00000000,
    0x00030047,0x00000017,0x00000002,0x00050048,0x00000017,0x00000000,0x00000023,0x00000000,
    0x00050048,0x00000017,0x00000001,0x00000023,0x00000004,0x00040047,0x0000002e,0x0000001e,
    0x00000000,0x00040047,0x00000030,0x0000001e,0x00000001,0x00020013,0x00000002,0x00030021,
    0x00000003,0x00000002,0x00030016,0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,
    0x00000004,0x00040015,0x00000008,0x00000020,0x00000000,0x0004002b,0x00000008,0x00000009,
    0x00000001,0x0004001c,0x0000000a,0x00000006,0x00000009,0x0006001e,0x0000000b,0x00000007,
    0x00000006,0x0000000a,0x0000000a,0x00040020,0x0000000c,0x00000003,0x0000000b,0x0004003b,
    0x0000000c,0x0000000d,0x00000003,0x00040015,0x0000000e,0x00000020,0x00000001,0x0004002b,
    0x0000000e,0x0000000f,0x00000000,0x00040017,0x00000010,0x00000006,0x00000002,0x00040020,
    0x00000011,0x00000001,0x00000010,0x0004003b,0x00000011,0x00000012,0x00000001,0x0004002b,
    0x00000008,0x00000013,0x00000000,0x00040020,0x00000014,0x00000001,0x00000006,0x0004001e,
    0x00000017,0x00000006,0x00000006,0x00040020,0x00000018,0x00000009,0x00000017,0x0004003b,
    0x00000018,0x00000019,0x00000009,0x00040020,0x0000001a,0x00000009,0x00000006,0x0004002b,
    0x00000006,0x0000001e,0x40000000,0x0004002b,0x00000006,0x00000020,0x3f800000,0x0004002b,
    0x0000000e,0x00000024,0x00000001,0x0004002b,0x00000006,0x0000002a,0x00000000,0x00040020,
    0x0000002c,0x00000003,0x00000007,0x0004003b,0x0000002c,0x0000002e,0x00000003,0x00040020,
    0x0000002f,0x00000001,0x00000007,0x0004003b,0x0000002f,0x00000030,0x00000001,0x00050036,
    0x00000002,0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x00050041,0x00000014,
    0x00000015,0x00000012,0x00000013,0x0004003d,0x00000006,0x00000016,0x00000015,0x00050041,
    0x0000001a,0x0000001b,0x00000019,0x0000000f,0x0004003d,0x00000006,0x0000001c,0x0000001b,
    0x00050088,0x00000006,0x0000001d,0x00000016,0x0000001c,0x00050085,0x00000006,0x0000001f,
    0x0000001d,0x0000001e,0x00050083,0x00000006,0x00000021,0x0000001f,0x00000020,0x00050041,
    0x00000014,0x00000022,0x00000012,0x00000009,0x0004003d,0x00000006,0x00000023,0x00000022,
    0x00050041,0x0000001a,0x00000025,0x00000019,0x00000024,0x0004003d,0x00000006,0x00000026,
    0x00000025,0x00050088,0x00000006,0x00000027,0x00000023,0x00000026,0x00050085,0x00000006,
    0x00000028,0x00000027,0x0000001e,0x00050083,0x00000006,0x00000029,0x00000028,0x00000020,
    0x00070050,0x00000007,0x0000002b,0x00000021,0x00000029,0x0000002a,0x00000020,0x00050041,
    0x0000002c,0x0000002d,0x0000000d,0x0000000f,0x0003003e,0x0000002d,0x0000002b,0x0004003d,
    0x00000007,0x00000031,0x00000030,0x0003003e,0x0000002e,0x00000031,0x000100fd,0x00010038,
};
static const uint32_t eg_frag_spv[]={
    0x07230203,0x00010000,0x0008000b,0x0000000d,0x00000000,0x00020011,0x00000001,0x0006000b,
    0x00000001,0x4c534c47,0x6474732e,0x3035342e,0x00000000,0x0003000e,0x00000000,0x00000001,
    0x0007000f,0x00000004,0x00000004,0x6e69616d,0x00000000,0x00000009,0x0000000b,0x00030010,
    0x00000004,0x00000007,0x00030003,0x00000002,0x000001c2,0x00040005,0x00000004,0x6e69616d,
    0x00000000,0x00050005,0x00000009,0x67617266,0x6f6c6f43,0x00000072,0x00040005,0x0000000b,
    0x6c6f6376,0x0000726f,0x00040047,0x00000009,0x0000001e,0x00000000,0x00040047,0x0000000b,
    0x0000001e,0x00000000,0x00020013,0x00000002,0x00030021,0x00000003,0x00000002,0x00030016,
    0x00000006,0x00000020,0x00040017,0x00000007,0x00000006,0x00000004,0x00040020,0x00000008,
    0x00000003,0x00000007,0x0004003b,0x00000008,0x00000009,0x00000003,0x00040020,0x0000000a,
    0x00000001,0x00000007,0x0004003b,0x0000000a,0x0000000b,0x00000001,0x00050036,0x00000002,
    0x00000004,0x00000000,0x00000003,0x000200f8,0x00000005,0x0004003d,0x00000007,0x0000000c,
    0x0000000b,0x0003003e,0x00000009,0x0000000c,0x000100fd,0x00010038,
};

#define EG_MAX_VERTS 65536
#define EG_VB_SIZE (EG_MAX_VERTS * 12)

typedef struct { float x, y; uint32_t color; } EgVert;

static inline uint32_t eg_argb_to_rgba(int argb) {
    uint32_t a = ((uint32_t)argb >> 24) & 0xFF;
    uint32_t r = ((uint32_t)argb >> 16) & 0xFF;
    uint32_t g = ((uint32_t)argb >> 8) & 0xFF;
    uint32_t b = (uint32_t)argb & 0xFF;
    return r | (g << 8) | (b << 16) | (a << 24);
}

static int eg_w2s(float wx,float wy,float wz,const GvFrame *view,float sw,float sh,float *sx,float *sy) {
    if(!view->valid || !view->width || !view->height) return 0;
    float relative[]={wx*100.0f+view->translation[0],wy*100.0f+view->translation[1],wz*100.0f+view->translation[2]};
    float clip[4];
    for(unsigned i=0;i<4;i++) clip[i]=relative[0]*view->matrix[i]+relative[1]*view->matrix[4+i]+relative[2]*view->matrix[8+i]+view->matrix[12+i];
    if(!isfinite(clip[3]) || clip[3]<=0.001f) return 0;
    float x=view->viewport.x+(0.5f+0.5f*clip[0]/clip[3])*view->viewport.width;
    float y=view->viewport.y+(0.5f+0.5f*clip[1]/clip[3])*view->viewport.height;
    float u=x/view->width,v=y/view->height;
    if(view->screen_rotation==1) {float old=u;u=v;v=1.0f-old;}
    else if(view->screen_rotation==2) {u=1.0f-u;v=1.0f-v;}
    else if(view->screen_rotation==3) {float old=u;u=1.0f-v;v=old;}
    *sx=u*sw;*sy=v*sh;
    return isfinite(*sx) && isfinite(*sy);
}

static inline void eg_line(EgVert *vb, int *n, float x0, float y0, float x1, float y1, uint32_t c) {
    if (*n + 6 > EG_MAX_VERTS) return;
    float dx = x1 - x0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.001f) return;
    float nx = -dy / len, ny = dx / len;
    float hw = 1.0f;
    float lx = nx * hw, ly = ny * hw;
    EgVert *v = vb + *n;
    v[0] = (EgVert){x0 - lx, y0 - ly, c};
    v[1] = (EgVert){x0 + lx, y0 + ly, c};
    v[2] = (EgVert){x1 + lx, y1 + ly, c};
    v[3] = (EgVert){x0 - lx, y0 - ly, c};
    v[4] = (EgVert){x1 + lx, y1 + ly, c};
    v[5] = (EgVert){x1 - lx, y1 - ly, c};
    *n += 6;
}

static inline void eg_rect(EgVert *vb, int *n, float l, float t, float r, float b, uint32_t c) {
    eg_line(vb, n, l, t, r, t, c);
    eg_line(vb, n, r, t, r, b, c);
    eg_line(vb, n, r, b, l, b, c);
    eg_line(vb, n, l, b, l, t, c);
}

static inline void eg_fill(EgVert *vb, int *n, float l, float t, float r, float b, uint32_t c) {
    if (*n + 6 > EG_MAX_VERTS) return;
    EgVert *v = vb + *n;
    v[0] = (EgVert){l, t, c}; v[1] = (EgVert){r, t, c}; v[2] = (EgVert){r, b, c};
    v[3] = (EgVert){l, t, c}; v[4] = (EgVert){r, b, c}; v[5] = (EgVert){l, b, c};
    *n += 6;
}

static _Atomic int eg_esp_on;
static _Atomic int eg_feat[5];
static _Atomic int eg_rdist[5];
static _Atomic int eg_palette[5][2];
static _Atomic int eg_snap_origin;

static _Atomic int eg_active;
static _Atomic int eg_menu_shown;
static pthread_mutex_t eg_scene_mutex=PTHREAD_MUTEX_INITIALIZER;
static Entity eg_entities[MAX_ACTORS];
static int eg_entity_count;
static char eg_self_id[32];
static int64_t eg_scene_at;
static unsigned eg_loot_mask=255;
static int eg_loot_flags=3;
static uint64_t eg_loot_minimum;
static void eg_publish(unsigned loot_mask,uint64_t loot_minimum,int loot_flags) {
    pthread_mutex_lock(&eg_scene_mutex);
    int count=num_entities;if(count<0) count=0;if(count>MAX_ACTORS) count=MAX_ACTORS;
    memcpy(eg_entities,entities,(size_t)count*sizeof(Entity));
    eg_entity_count=count;memcpy(eg_self_id,self_entity_id,sizeof(eg_self_id));eg_self_id[sizeof(eg_self_id)-1]=0;
    eg_scene_at=ef_now();eg_loot_mask=loot_mask;eg_loot_minimum=loot_minimum;eg_loot_flags=loot_flags;
    pthread_mutex_unlock(&eg_scene_mutex);
}
static int eg_draw(EgVert *vb,float sw,float sh,const GvFrame *view) {
    if(!atomic_load(&eg_esp_on) || !view->valid) {atomic_store(&eg_active,0);return 0;}
    pthread_mutex_lock(&eg_scene_mutex);
    if(ef_now()-eg_scene_at>200) {atomic_store(&eg_active,0);pthread_mutex_unlock(&eg_scene_mutex);return 0;}
    int n_ents=eg_entity_count;
    atomic_store(&eg_active,1);
    int vn = 0;
    int snap_origin = atomic_load(&eg_snap_origin);

    for (int i = 0; i < n_ents && i < MAX_ACTORS; i++) {
        const Entity *e = &eg_entities[i];
        if (strcmp(e->id, eg_self_id) == 0) continue;
        if (e->dead) continue;

        int etype=actor_display_type(e->kind[0],e->is_player_bot,e->team);

        int ef = atomic_load(&eg_feat[etype]);
        if (!(ef & 1)) continue;

        float relative[3]={e->x+view->translation[0]/100.0f,e->y+view->translation[1]/100.0f,e->z+view->translation[2]/100.0f};
        float dist=sqrtf(relative[0]*relative[0]+relative[1]*relative[1]+relative[2]*relative[2]);
        int rd = atomic_load(&eg_rdist[etype]);
        if (dist > (float)rd) continue;

        if (etype == 4) {
            if(!loot_matches(e->loot_quality,e->loot_metadata_known,e->loot_unit_value,e->loot_value_known,
                e->loot_kind!=LOOT_ITEM || e->loot_in_container,eg_loot_mask,eg_loot_minimum,(eg_loot_flags&2)!=0,dist,(float)rd)) continue;
            float sx, sy;
            if (!eg_w2s(e->x, e->y, e->z, view, sw, sh, &sx, &sy)) continue;
            if (sx < -20 || sx > sw + 20 || sy < -20 || sy > sh + 20) continue;
            int color_argb = e->loot_color_known ? (int)e->loot_color : (int)0xffdedede;
            uint32_t c = eg_argb_to_rgba(color_argb);
            if (ef & 2) eg_fill(vb, &vn, sx - 3, sy - 3, sx + 3, sy + 3, c);
            continue;
        }

        float hh_m = e->half_height > 0.1f ? e->half_height : 1.05f;
        float sx_head, sy_head, sx_feet, sy_feet;
        if (!eg_w2s(e->x, e->y, e->z + hh_m, view, sw, sh, &sx_head, &sy_head) ||
            !eg_w2s(e->x, e->y, e->z - hh_m, view, sw, sh, &sx_feet, &sy_feet))
            continue;

        float box_top = sy_head < sy_feet ? sy_head : sy_feet;
        float box_bot = sy_head < sy_feet ? sy_feet : sy_head;
        float box_cx = (sx_head + sx_feet) * 0.5f;
        float box_h = box_bot - box_top;
        if (box_h < 10.0f) box_h = 10.0f;
        box_bot = box_top + box_h;
        float bw = box_h * 0.45f * 0.5f;

        int vis = e->visibility_known ? (e->visible_from_camera != 0) : -1;
        int color_argb = vis >= 0 ? atomic_load(&eg_palette[etype][vis]) : (int)0xFFFFB300;
        uint32_t c = eg_argb_to_rgba(color_argb);

        int box_visible = !(box_cx + bw < 0 || box_cx - bw > sw ||
            box_bot < 0 || box_top > sh);

        if ((ef & 2) && box_visible)
            eg_rect(vb, &vn, box_cx - bw, box_top, box_cx + bw, box_bot, c);

        if ((ef & 4) && e->has_bones && e->bone_count >= 2 &&
            e->bone_count <= MAX_SKELETON_BONES) {
            for (int j = 0; j < e->bone_count; j++) {
                int parent = e->bone_parent[j];
                if (parent < 0 || parent >= j) continue;
                float ax, ay, bx, by;
                if (!eg_w2s(e->bone_pos[j][0], e->bone_pos[j][1], e->bone_pos[j][2],
                            view, sw, sh, &ax, &ay) ||
                    !eg_w2s(e->bone_pos[parent][0], e->bone_pos[parent][1], e->bone_pos[parent][2],
                            view, sw, sh, &bx, &by)) continue;
                eg_line(vb, &vn, ax, ay, bx, by, c);
            }
        }

        if (!box_visible) continue;

        if ((ef & 64) && box_visible) {
            float snap_y = snap_origin ? 0.0f : sh;
            eg_line(vb, &vn, sw * 0.5f, snap_y, box_cx,
                    snap_origin ? box_top : box_bot, c);
        }

        if ((ef & 16) && e->health_known && e->max_health > 0) {
            float hp = (float)e->health / (float)e->max_health;
            if (hp < 0) hp = 0; if (hp > 1) hp = 1;
            float bar_h = box_h * hp;
            int bar_r = (int)((1.0f - hp) * 255);
            int bar_g = (int)(hp * 255);
            int bar_argb = (int)(0xFF000000u | ((uint32_t)bar_r << 16) | ((uint32_t)bar_g << 8));
            eg_fill(vb, &vn, box_cx - bw - 6, box_bot - bar_h,
                    box_cx - bw - 2, box_bot, eg_argb_to_rgba(bar_argb));
        }
    }
    pthread_mutex_unlock(&eg_scene_mutex);
    return vn;
}

#endif
