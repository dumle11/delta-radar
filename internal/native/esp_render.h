/* ESP config + Vulkan chams — included by delta_radar_lib.c
 * Config atomics shared between overlay (esp_overlay.h) and chams hooks.
 * ESP/UI are composited into the game swapchain by vulkan_present.h.
 * Chams uses inline hooks on the Vulkan ICD (Adreno). */
#ifndef INTERNAL_ESP_RENDER_H
#define INTERNAL_ESP_RENDER_H

/* Config atomics — scanner thread writes, render thread reads */
#define ER_ET_COUNT 5
static _Atomic int _er_feat[ER_ET_COUNT];
static _Atomic int _er_rdist[ER_ET_COUNT];
static _Atomic int _er_palette[ER_ET_COUNT][2];
static _Atomic int _er_esp_on;
static _Atomic int _er_snap_origin;
static _Atomic int _er_chams_enabled;
static _Atomic unsigned _er_chams_vis;
static _Atomic unsigned _er_chams_inv;
static _Atomic int _er_chams_brightness = 70;

#define EF_ENABLED  1
#define EF_BOXES    2
#define EF_SKELETON 4
#define EF_NAMES    8
#define EF_HEALTH   16
#define EF_DISTANCE 32
#define EF_SNAPLINES 64

__attribute__((noinline))
static void *_gsym(const unsigned char *enc, int len) {
    char buf[48];
    volatile unsigned char k = 0x55;
    for (int i = 0; i < len && i < 47; i++) buf[i] = enc[i] ^ k;
    buf[len] = 0;
    return dlsym(RTLD_DEFAULT, buf);
}

/* ---- Vulkan chams via ICD inline hooks ---- */
#if defined(__ANDROID__) && defined(__aarch64__)

#include "gpu_view.h"

typedef void *VkDev_;
typedef void *VkCmdBuf_;
typedef void *VkPipe_;
typedef void *VkPipeCache_;

typedef struct {
    uint32_t sType;
    const void *pNext;
    uint32_t flags, depthTestEnable, depthWriteEnable, depthCompareOp;
    uint32_t depthBoundsTestEnable, stencilTestEnable;
    uint32_t stencilFront[7], stencilBack[7];
    float minDepthBounds, maxDepthBounds;
} VkDSSCI_;

typedef struct {
    uint32_t sType; const void *pNext;
    uint32_t flags, stage;
    void *module; const char *pName; const void *pSpec;
} VkPSSCI_;

typedef struct {
    uint32_t sType; const void *pNext; uint32_t flags;
    uint32_t depthClampEnable, rasterizerDiscardEnable, polygonMode, cullMode, frontFace;
    uint32_t depthBiasEnable; float depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor;
    float lineWidth;
} VkRSCI_;

typedef struct {
    uint32_t sType;
    const void *pNext;
    uint32_t flags, stageCount;
    const VkPSSCI_ *pStages;
    const void *pVertexInputState, *pInputAssemblyState;
    const void *pTessellationState, *pViewportState;
    const VkRSCI_ *pRasterizationState;
    const void *pMultisampleState;
    const VkDSSCI_ *pDepthStencilState;
    const void *pColorBlendState, *pDynamicState;
    void *layout, *renderPass;
    uint32_t subpass;
    void *basePipelineHandle;
    int32_t basePipelineIndex;
} VkGPCI_;

typedef struct {
    uint32_t binding; uint32_t stride; uint32_t inputRate;
} VkVIBD_;

typedef struct {
    uint32_t location; uint32_t binding; uint32_t format; uint32_t offset;
} VkVIAD_;

typedef struct {
    uint32_t sType; const void *pNext;
    uint32_t flags;
    uint32_t bindingCount; const VkVIBD_ *pBindings;
    uint32_t attribCount; const VkVIAD_ *pAttribs;
} VkVISCI_;

typedef struct {
    uint32_t blendEnable;
    uint32_t srcColorBlendFactor, dstColorBlendFactor, colorBlendOp;
    uint32_t srcAlphaBlendFactor, dstAlphaBlendFactor, alphaBlendOp;
    uint32_t colorWriteMask;
} VkCBAS_;

typedef struct {
    uint32_t sType; const void *pNext;
    uint32_t flags, logicOpEnable, logicOp, attachmentCount;
    const VkCBAS_ *pAttachments;
    float blendConstants[4];
} VkCBSCI_;

typedef struct {
    uint32_t sType; const void *pNext;
    uint32_t flags, dynamicStateCount;
    const uint32_t *pDynamicStates;
} VkDynSCI_;

typedef void (*PFN_vkDI)(VkCmdBuf_, uint32_t, uint32_t, uint32_t, int32_t, uint32_t);
typedef void (*PFN_vkBP)(VkCmdBuf_, uint32_t, VkPipe_);
typedef int  (*PFN_vkCGP)(VkDev_, VkPipeCache_, uint32_t, const VkGPCI_*, const void*, VkPipe_*);
typedef int  (*PFN_vkCSM)(VkDev_, const void*, const void*, void**);
typedef void (*PFN_vkSV)(VkCmdBuf_, uint32_t, uint32_t, const void*);

static PFN_vkDI  _o_vk_di;
static PFN_vkBP  _o_vk_bp;
static PFN_vkCGP _o_vk_cgp;
static PFN_vkCSM _vk_csm;
static PFN_vkSV  _o_vk_sv;
static VkDev_    _vk_dev;
static int cr_ready;
static void     *_vk_frag_vis; /* shader module for visible color */
static void     *_vk_frag_inv; /* shader module for invisible color */
static volatile float _vk_vp_depth_range = 1.0f;

/* SPIR-V template: fragment shader that discards if gl_FragCoord.z > threshold
 * (filters first-person weapon in reverse-Z), then outputs vec4(R,G,B,1).
 * Patchable: R at word 61, G at word 65, B at word 69, threshold at word 77. */
#define SPV_COLOR_WORDS 123
#define SPV_OFF_R 61
#define SPV_OFF_G 65
#define SPV_OFF_B 69
#define SPV_OFF_DEPTH 77
static const uint32_t _spv_color_tpl[SPV_COLOR_WORDS] = {
    /* header */
    0x07230203, 0x00010000, 0x00000000, 0x00000017, 0x00000000,
    /* OpCapability Shader */
    0x00020011, 0x00000001,
    /* OpMemoryModel Logical GLSL450 */
    0x0003000E, 0x00000000, 0x00000001,
    /* OpEntryPoint Fragment %1 "main" %7 %F */
    0x0007000F, 0x00000004, 0x00000001, 0x6E69616D, 0x00000000, 0x00000007, 0x0000000F,
    /* OpExecutionMode %1 OriginUpperLeft */
    0x00030010, 0x00000001, 0x00000007,
    /* OpDecorate %7 Location 0 */
    0x00040047, 0x00000007, 0x0000001E, 0x00000000,
    /* OpDecorate %F BuiltIn FragCoord */
    0x00040047, 0x0000000F, 0x0000000B, 0x0000000F,
    /* types */
    0x00020013, 0x00000002, /* void */
    0x00030021, 0x00000003, 0x00000002, /* func void() */
    0x00030016, 0x00000004, 0x00000020, /* float32 */
    0x00040017, 0x00000005, 0x00000004, 0x00000004, /* vec4 */
    0x00040020, 0x00000006, 0x00000003, 0x00000005, /* ptr Output vec4 */
    0x0004003B, 0x00000006, 0x00000007, 0x00000003, /* %7 output var */
    0x00040020, 0x0000000E, 0x00000001, 0x00000005, /* ptr Input vec4 */
    0x0004003B, 0x0000000E, 0x0000000F, 0x00000001, /* %F FragCoord var */
    0x00020014, 0x00000010, /* bool */
    /* constants (patchable) */
    0x0004002B, 0x00000004, 0x00000008, 0x3F800000, /* R  — patch [61] */
    0x0004002B, 0x00000004, 0x00000009, 0x3F800000, /* G  — patch [65] */
    0x0004002B, 0x00000004, 0x0000000A, 0x3F800000, /* B  — patch [69] */
    0x0004002B, 0x00000004, 0x0000000B, 0x3F800000, /* alpha = 1.0 */
    0x0004002B, 0x00000004, 0x00000011, 0x3E19999A, /* threshold — patch [77] default 0.15 */
    /* composite color */
    0x0007002C, 0x00000005, 0x0000000C, 0x00000008, 0x00000009, 0x0000000A, 0x0000000B,
    /* function main */
    0x00050036, 0x00000002, 0x00000001, 0x00000000, 0x00000003,
    0x000200F8, 0x0000000D, /* label entry */
    /* load FragCoord, extract .z */
    0x0004003D, 0x00000005, 0x00000012, 0x0000000F, /* %12 = load FragCoord */
    0x00050051, 0x00000004, 0x00000013, 0x00000012, 0x00000002, /* %13 = .z */
    /* if (z > threshold) discard */
    0x000500BA, 0x00000010, 0x00000014, 0x00000013, 0x00000011, /* %14 = z > thresh */
    0x000300F7, 0x00000016, 0x00000000, /* selection merge %16 */
    0x000400FA, 0x00000014, 0x00000015, 0x00000016, /* branch %14 → kill : merge */
    0x000200F8, 0x00000015, /* label kill */
    0x000100FC,             /* OpKill */
    0x000200F8, 0x00000016, /* label merge */
    /* output color */
    0x0003003E, 0x00000007, 0x0000000C,
    0x000100FD, /* return */
    0x00010038  /* function end */
};

static volatile float _vk_depth_thresh = 0.015f;

static void *_vk_make_color_shader(VkDev_ dev, float r, float g, float b) {
    if (!_vk_csm) return NULL;
    uint32_t spv[SPV_COLOR_WORDS];
    memcpy(spv, _spv_color_tpl, sizeof(spv));
    uint32_t ri, gi, bi, ti;
    float thresh = _vk_depth_thresh;
    memcpy(&ri, &r, 4); memcpy(&gi, &g, 4); memcpy(&bi, &b, 4); memcpy(&ti, &thresh, 4);
    spv[SPV_OFF_R] = ri; spv[SPV_OFF_G] = gi; spv[SPV_OFF_B] = bi; spv[SPV_OFF_DEPTH] = ti;
    struct { uint32_t sType; const void *pNext; uint32_t flags;
             uint32_t _p; uint64_t codeSize; const uint32_t *pCode; } smci;
    memset(&smci, 0, sizeof(smci));
    smci.sType = 16;
    smci.codeSize = sizeof(spv);
    smci.pCode = spv;
    void *mod = NULL;
    if (_vk_csm(dev, &smci, NULL, &mod) == 0 && mod) return mod;
    return NULL;
}

/* Two-clone hash map: orig → occluded + visible (reverse-Z depth tests). */
#define VK_PM_BITS 11
#define VK_PM_CAP  (1 << VK_PM_BITS)
#define VK_PM_MASK (VK_PM_CAP - 1)
static struct { VkPipe_ orig, inv, vis; uint8_t nattr; uint8_t stride; } _vk_pm[VK_PM_CAP];
static volatile int _vk_pm_n;

static void _vk_pm_lookup(VkPipe_ orig, VkPipe_ *inv, VkPipe_ *vis, uint8_t *nattr, uint8_t *stride) {
    *inv = NULL; *vis = NULL; *nattr = 0; *stride = 0;
    if (!orig) return;
    uint32_t h = ((uintptr_t)orig >> 4) & VK_PM_MASK;
    for (int i = 0; i < 32; i++) {
        uint32_t idx = (h + i) & VK_PM_MASK;
        if (_vk_pm[idx].orig == orig) { *inv = _vk_pm[idx].inv; *vis = _vk_pm[idx].vis; *nattr = _vk_pm[idx].nattr; *stride = _vk_pm[idx].stride; return; }
        if (!_vk_pm[idx].orig) return;
    }
}

static void _vk_pm_add(VkPipe_ orig, VkPipe_ inv, VkPipe_ vis, uint8_t nattr, uint8_t stride) {
    uint32_t h = ((uintptr_t)orig >> 4) & VK_PM_MASK;
    for (int i = 0; i < 32; i++) {
        uint32_t idx = (h + i) & VK_PM_MASK;
        if (!_vk_pm[idx].orig) {
            _vk_pm[idx].orig = orig;
            _vk_pm[idx].inv = inv;
            _vk_pm[idx].vis = vis;
            _vk_pm[idx].nattr = nattr;
            _vk_pm[idx].stride = stride;
            _vk_pm_n++;
            return;
        }
        if (_vk_pm[idx].orig == orig) {
            _vk_pm[idx].inv = inv;
            _vk_pm[idx].vis = vis;
            _vk_pm[idx].nattr = nattr;
            _vk_pm[idx].stride = stride;
            return;
        }
    }
}

static volatile int _vk_shdr_fb;

/* Per-command-buffer pipeline + viewport tracking (render thread only) */
#define VK_CB_CAP 16
static struct { VkCmdBuf_ cmd; VkPipe_ pipe; float dr; } _vk_cb[VK_CB_CAP];

static VkPipe_ _vk_cur(VkCmdBuf_ cmd) {
    for (int i = 0; i < VK_CB_CAP; i++)
        if (_vk_cb[i].cmd == cmd) return _vk_cb[i].pipe;
    return NULL;
}

static void _vk_set(VkCmdBuf_ cmd, VkPipe_ pipe) {
    for (int i = 0; i < VK_CB_CAP; i++) {
        if (_vk_cb[i].cmd == cmd || !_vk_cb[i].cmd) {
            _vk_cb[i].cmd = cmd;
            _vk_cb[i].pipe = pipe;
            return;
        }
    }
    _vk_cb[0].cmd = cmd;
    _vk_cb[0].pipe = pipe;
}

static void _vk_set_dr(VkCmdBuf_ cmd, float dr) {
    for (int i = 0; i < VK_CB_CAP; i++) {
        if (_vk_cb[i].cmd == cmd || !_vk_cb[i].cmd) {
            _vk_cb[i].cmd = cmd;
            _vk_cb[i].dr = dr;
            return;
        }
    }
    _vk_cb[0].dr = dr;
}

static float _vk_get_dr(VkCmdBuf_ cmd) {
    for (int i = 0; i < VK_CB_CAP; i++)
        if (_vk_cb[i].cmd == cmd) return _vk_cb[i].dr;
    return 1.0f;
}

static void _vk_ensure_shaders(VkDev_ dev, float vr, float vg, float vb,
                                float ir, float ig, float ib) {
    if (_vk_frag_vis && _vk_frag_inv) return;
    if (!_vk_csm) return;
    if (!_vk_frag_vis) {
        _vk_frag_vis = _vk_make_color_shader(dev, vr, vg, vb);
        char db[96]; snprintf(db,sizeof(db),"v0: vis_shdr r=%.3f g=%.3f b=%.3f p=%p",vr,vg,vb,_vk_frag_vis);
        _diag(db);
    }
    if (!_vk_frag_inv) {
        _vk_frag_inv = _vk_make_color_shader(dev, ir, ig, ib);
        char db[96]; snprintf(db,sizeof(db),"v0: inv_shdr r=%.3f g=%.3f b=%.3f p=%p",ir,ig,ib,_vk_frag_inv);
        _diag(db);
    }
}

static VkPipe_ _vk_make_clone(VkDev_ dev, VkPipeCache_ cache,
    const VkGPCI_ *src, const void *alloc, int behind_wall,
    void *frag_mod) {
    if(!cr_ready) return NULL;
    VkGPCI_ ci;
    memcpy(&ci, src, sizeof(ci));

    VkDSSCI_ cds;
    memcpy(&cds, src->pDepthStencilState, sizeof(cds));
    cds.stencilTestEnable = 0;
    cds.depthTestEnable = 1;
    cds.depthBoundsTestEnable = 0;
    VkRSCI_ crs;
    memcpy(&crs, src->pRasterizationState, sizeof(crs));
    crs.depthBiasEnable = 0;
    if (behind_wall) {
        cds.depthCompareOp = 1; /* VK_COMPARE_OP_LESS — passes where behind geometry (reverse-Z) */
        cds.depthWriteEnable = 0;
    } else {
        cds.depthCompareOp = 6; /* GREATER_OR_EQUAL in reverse-Z */
        /* Visible clone must cover the red pass at the front surface. */
        cds.depthWriteEnable = 0;
    }
    ci.pDepthStencilState = &cds;
    ci.pRasterizationState = &crs;
    ci.basePipelineHandle = NULL;
    ci.basePipelineIndex = -1;

    /* Replace fragment shader with solid-color output */
    VkPSSCI_ stages[8];
    int replaced = 0;
    if (frag_mod && src->stageCount <= 8) {
        memcpy(stages, src->pStages, src->stageCount * sizeof(VkPSSCI_));
        for (uint32_t s = 0; s < src->stageCount; s++) {
            if (stages[s].stage == 0x10) { /* VK_SHADER_STAGE_FRAGMENT_BIT */
                stages[s].module = frag_mod;
                stages[s].pName = "main";
                stages[s].pSpec = NULL;
                replaced = 1;
                break;
            }
        }
        if (replaced) ci.pStages = stages;
    }

    /* Color blend: white shader * live tint, no destination contribution. */
    VkCBSCI_ ccbs;
    VkCBAS_ catt[8];
    const VkCBSCI_ *cbs = (const VkCBSCI_ *)src->pColorBlendState;
    uint32_t ac = 1;
    if (cbs) {
        memcpy(&ccbs, cbs, sizeof(ccbs));
        ac = cbs->attachmentCount;
        if (ac > 8) ac = 8;
        if (ac > 0 && cbs->pAttachments)
            memcpy(catt, cbs->pAttachments, ac * sizeof(VkCBAS_));
    } else {
        memset(&ccbs, 0, sizeof(ccbs));
        ccbs.sType = 26;
        memset(catt, 0, sizeof(VkCBAS_));
        catt[0].colorWriteMask = 0xF;
    }
    for (uint32_t a = 0; a < ac; a++) {
        catt[a].colorWriteMask = a == 0 ? 0xF : 0;
        catt[a].blendEnable = 1;
        catt[a].srcColorBlendFactor = 10; /* CONSTANT_COLOR: live tint */
        catt[a].dstColorBlendFactor = 0;  /* VK_BLEND_FACTOR_ZERO */
        catt[a].colorBlendOp = 0;
        catt[a].srcAlphaBlendFactor = 1;  /* VK_BLEND_FACTOR_ONE */
        catt[a].dstAlphaBlendFactor = 0;
        catt[a].alphaBlendOp = 0;
    }
    ccbs.logicOpEnable = 0;
    ccbs.attachmentCount = ac;
    ccbs.pAttachments = catt;
    ci.pColorBlendState = (const void *)&ccbs;

    /* Tint uses a white fragment shader and dynamic blend constants, so colors
     * and intensity can change without rebuilding modules or pipelines. */
    VkDynSCI_ dynamic = {0};
    uint32_t states[16];
    const VkDynSCI_ *original_dynamic = (const VkDynSCI_*)src->pDynamicState;
    if(original_dynamic) dynamic=*original_dynamic;
    else dynamic.sType=27;
    if(dynamic.dynamicStateCount>15) return NULL;
    int has_blend=0;
    for(uint32_t i=0;i<dynamic.dynamicStateCount;i++) {
        states[i]=dynamic.pDynamicStates[i];
        if(states[i]>8) return NULL; /* journal supports core 1.0 dynamic state */
        if(states[i]==4) has_blend=1;
    }
    if(!has_blend) states[dynamic.dynamicStateCount++]=4;
    dynamic.pDynamicStates=states;
    ci.pDynamicState=&dynamic;

    VkPipe_ cp = NULL;
    int rc = _o_vk_cgp(dev, cache, 1, &ci, alloc, &cp);
    if ((rc != 0 || !cp) && replaced) {
        _vk_shdr_fb++;
        static int fb_logged;
        if (!fb_logged) {
            const char *orig_name = "?";
            uint32_t orig_stage_flags = 0;
            for (uint32_t s = 0; s < src->stageCount; s++) {
                if (src->pStages[s].stage == 0x10) {
                    orig_name = src->pStages[s].pName ? src->pStages[s].pName : "null";
                    orig_stage_flags = src->pStages[s].flags;
                    break;
                }
            }
            char db[96]; snprintf(db,sizeof(db),"v0: shdr fail rc=%d ac=%u name=%.20s fl=%u",
                rc, ac, orig_name, orig_stage_flags);
            _diag(db); fb_logged=1;
        }
        /* Retry without shader replacement */
        ci.pStages = src->pStages;
        cp = NULL;
        rc = _o_vk_cgp(dev, cache, 1, &ci, alloc, &cp);
    }
    if (rc != 0 || !cp) return NULL;
    return cp;
}

static int _hook_vk_cgp(VkDev_ dev, VkPipeCache_ cache, uint32_t cnt,
    const VkGPCI_ *infos, const void *alloc, VkPipe_ *pipes) {
    if (!_vk_dev) _vk_dev = dev;
    _vk_ensure_shaders(dev,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f);
    int r = _o_vk_cgp(dev, cache, cnt, infos, alloc, pipes);
    if (r != 0) return r;
    static int _shdr_ok, _shdr_fail, _attr_skip, _fp_skip, _pipe_logged, _depth_yes, _depth_no;
    for (uint32_t i = 0; i < cnt; i++) {
        const VkDSSCI_ *ds = infos[i].pDepthStencilState;
        if (!ds || !ds->depthTestEnable) { _depth_no++; continue; }
        _depth_yes++;
        if (infos[i].stageCount < 2 || infos[i].stageCount > 8) continue;
        const VkVISCI_ *vis = (const VkVISCI_ *)infos[i].pVertexInputState;
        uint32_t ac = vis ? vis->attribCount : 0;
        uint32_t bc = vis ? vis->bindingCount : 0;
        uint32_t max_stride = 0;
        if (vis && vis->pBindings) {
            for (uint32_t b = 0; b < bc; b++)
                if (vis->pBindings[b].stride > max_stride)
                    max_stride = vis->pBindings[b].stride;
        }
        int has_bone_idx = 0, has_bone_wgt = 0;
        if (vis && vis->pAttribs) {
            for (uint32_t a2 = 0; a2 < ac; a2++) {
                uint32_t fmt = vis->pAttribs[a2].format;
                if (fmt == 41) has_bone_idx = 1;
                if (fmt == 37) has_bone_wgt = 1;
            }
        }
        int has_bone_fmt = has_bone_idx && has_bone_wgt;
        const VkCBSCI_ *cbs_info = (const VkCBSCI_ *)infos[i].pColorBlendState;
        uint32_t n_attach = cbs_info ? cbs_info->attachmentCount : 0;
        if (_pipe_logged < 20 && has_bone_fmt) {
            char db[96]; snprintf(db,sizeof(db),"v0: skel a=%u b=%u s=%u att=%u cop=%u dw=%u",
                ac,bc,max_stride,n_attach,
                ds->depthCompareOp, ds->depthWriteEnable);
            _diag(db); _pipe_logged++;
        }
        /* The same validated packed scene-view expression also occurs in
         * scenery draws. ESP must have a current view when no actor is drawn. */
        for(uint32_t stage=0;n_attach>0 && stage<infos[i].stageCount;stage++)
            if(infos[i].pStages[stage].stage==1) gv_pipeline((VkPipeline)pipes[i],(VkShaderModule)infos[i].pStages[stage].module);
        if (!has_bone_fmt) { _attr_skip++; continue; }
        if (n_attach < 1) { _attr_skip++; continue; }
        if (ac <= 5) { _fp_skip++; continue; }

        for(uint32_t stage=0;stage<infos[i].stageCount;stage++)
            if(infos[i].pStages[stage].stage==1) { gv_selected((VkShaderModule)infos[i].pStages[stage].module); }
        VkPipe_ cinv = _vk_make_clone(dev, cache, &infos[i], alloc,
            1, _vk_frag_inv);
        VkPipe_ cvis = _vk_make_clone(dev, cache, &infos[i], alloc,
            0, _vk_frag_vis);
        if (cinv && cvis) {
            _vk_pm_add(pipes[i], cinv, cvis, (uint8_t)ac, (uint8_t)(max_stride & 0xFF));
            _shdr_ok++;
        } else {
            _shdr_fail++;
        }
    }
    static int logged; static int64_t next_log;
    if (!logged || ef_now() >= next_log) {
        char db[160]; snprintf(db,sizeof(db),"v0: cgp cnt=%u cl=%d ok=%d fail=%d fb=%d skip=%d fp=%d fv=%d fi=%d dy=%d dn=%d",
            cnt,_vk_pm_n,_shdr_ok,_shdr_fail,_vk_shdr_fb,_attr_skip,_fp_skip,!!_vk_frag_vis,!!_vk_frag_inv,_depth_yes,_depth_no);
        _diag(db); logged=1; next_log=ef_now()+5000;
    }
    return r;
}

static void cr_pipeline(VkCmdBuf_ cmd, VkPipe_ pipe);
static void cr_color(VkCmdBuf_ cmd, int visible);
static int cr_capture(VkCmdBuf_ cmd, VkPipe_ inv, VkPipe_ vis,
    uint32_t ic, uint32_t inst, uint32_t fi, int32_t vo, uint32_t fInst);

static void _hook_vk_bp(VkCmdBuf_ cmd, uint32_t bp, VkPipe_ pipe) {
    if (bp == 0) { _vk_set(cmd, pipe); cr_pipeline(cmd,pipe); }
    _o_vk_bp(cmd, bp, pipe);
}

static void _hook_vk_sv(VkCmdBuf_ cmd, uint32_t first, uint32_t cnt,
    const void *vps) {
    if (cnt > 0 && vps) {
        const float *vp = (const float *)vps;
        float range = vp[5] - vp[4]; /* maxDepth - minDepth */
        _vk_vp_depth_range = range;
        _vk_set_dr(cmd, range);
    }
    _o_vk_sv(cmd, first, cnt, vps);
}

static void cr_log(void);
static volatile int _vk_chams_cnt;
static volatile int _vk_chams_miss;
static volatile int _vk_fp_skip;

static volatile int _vk_ic_hist[4]; /* 0:<10k 1:10-20k 2:20-50k 3:50k+ */
static volatile int _vk_na_hist[4]; /* 0:a<=6 1:a=7 2:a=8 3:a>=9 */

static void _hook_vk_di(VkCmdBuf_ cmd, uint32_t ic, uint32_t inst,
    uint32_t fi, int32_t vo, uint32_t fInst) {
    static int _vk_logged;
    if (!_vk_logged) { _diag("v1: draw active"); _vk_logged = 1; }
    VkPipe_ cur = _vk_cur(cmd);
    if(inst == 1 && ic > 0 && cur) gv_draw_view((VkCommandBuffer)cmd,(VkPipeline)cur);
    if (atomic_load(&_er_chams_enabled) && inst == 1 && ic > 0) {
        VkPipe_ cinv, cvis; uint8_t nattr, stride;
        _vk_pm_lookup(cur, &cinv, &cvis, &nattr, &stride);
        if (cinv && cvis) {
            if (ic < 10000) _vk_ic_hist[0]++;
            else if (ic < 20000) _vk_ic_hist[1]++;
            else if (ic < 50000) _vk_ic_hist[2]++;
            else _vk_ic_hist[3]++;
            if (nattr <= 6) _vk_na_hist[0]++;
            else if (nattr == 7) _vk_na_hist[1]++;
            else if (nattr == 8) _vk_na_hist[2]++;
            else _vk_na_hist[3]++;
            _vk_chams_cnt++;
            if (cr_capture(cmd,cinv,cvis,ic,inst,fi,vo,fInst)) {
                _o_vk_di(cmd,ic,inst,fi,vo,fInst);
                goto log_check;
            }
            /* Red first: a later hidden triangle must not overwrite the
             * green front surface of the same draw. Neither clone writes depth. */
            _o_vk_bp(cmd, 0, cinv);
            cr_color(cmd,0);
            _o_vk_di(cmd, ic, inst, fi, vo, fInst);
            _o_vk_bp(cmd, 0, cur);
            _o_vk_di(cmd, ic, inst, fi, vo, fInst);
            _o_vk_bp(cmd, 0, cvis);
            cr_color(cmd,1);
            _o_vk_di(cmd, ic, inst, fi, vo, fInst);
            /* Restore original pipeline */
            _o_vk_bp(cmd, 0, cur);
            goto log_check;
        } else if (cur) {
            _vk_chams_miss++;
        }
    }
do_orig:
    _o_vk_di(cmd, ic, inst, fi, vo, fInst);
log_check:;
    static int64_t _vk_di_next;
    if (ef_now() >= _vk_di_next) {
        int cc = _vk_chams_cnt, cm = _vk_chams_miss, fs = _vk_fp_skip;
        _vk_chams_cnt = 0; _vk_chams_miss = 0; _vk_fp_skip = 0;
        char db[200]; snprintf(db,sizeof(db),"v1: hit=%d miss=%d fs=%d cl=%d en=%d ic=<%d/%d/%d/%d> na=<%d/%d/%d/%d>",
            cc, cm, fs, _vk_pm_n, atomic_load(&_er_chams_enabled),
            _vk_ic_hist[0], _vk_ic_hist[1], _vk_ic_hist[2], _vk_ic_hist[3],
            _vk_na_hist[0], _vk_na_hist[1], _vk_na_hist[2], _vk_na_hist[3]);
        _diag(db); cr_log(); _vk_di_next = ef_now() + 5000;
        _vk_ic_hist[0]=0; _vk_ic_hist[1]=0; _vk_ic_hist[2]=0; _vk_ic_hist[3]=0;
        _vk_na_hist[0]=0; _vk_na_hist[1]=0; _vk_na_hist[2]=0; _vk_na_hist[3]=0;
    }
}

#include "chams_reloc.h"

static int _ih_patch(void *target, void *hook, void **trampoline, uint8_t *tramp_slot) {
    uint32_t relocated[16];
    unsigned length=cr_trampoline(relocated,(const uint32_t*)target,(uintptr_t)target);
    if(!length) { _diag("v2: unsupported hook prologue"); return 0; }
    memcpy(tramp_slot,relocated,length);
    __builtin___clear_cache((char*)tramp_slot,(char*)tramp_slot+length);
    long ps = sysconf(_SC_PAGESIZE);
    uintptr_t page = (uintptr_t)target & ~(ps - 1);
    uintptr_t end  = ((uintptr_t)target + 16 + ps - 1) & ~(ps - 1);
    if (syscall(__NR_mprotect, (void *)page, end - page,
                PROT_READ | PROT_WRITE | PROT_EXEC) != 0) return 0;
    *trampoline = tramp_slot;
    uint32_t ldr_x16 = 0x58000050;
    uint32_t br_x16  = 0xD61F0200;
    uint8_t *p = (uint8_t *)target;
    memcpy(p, &ldr_x16, 4);
    memcpy(p + 4, &br_x16, 4);
    uint64_t ha = (uint64_t)(uintptr_t)hook;
    memcpy(p + 8, &ha, 8);
    __builtin___clear_cache((char *)target, (char *)target + 16);
    syscall(__NR_mprotect, (void *)page, end - page, PROT_READ | PROT_EXEC);
    return 1;
}

__attribute__((noinline))
static void *_vksym(void *(*lookup)(void*,const char*), void *ctx,
                    const unsigned char *enc, int len) {
    char buf[48];
    volatile unsigned char k = 0x55;
    for (int i = 0; i < len && i < 47; i++) buf[i] = enc[i] ^ k;
    buf[len] = 0;
    return lookup(ctx, buf);
}

#include "chams_replay.h"
#include "gpu_view_runtime.h"
#include "vulkan_present.h"

static void _install_esp_hook(void) {
    if (_o_vk_di) return;

    /* Open Vulkan loader (always visible to app, unlike vendor ICD) */
    /* "libvulkan.so" XOR 0x55 */
    char vklib[16];
    { static const unsigned char vl[] = {0x39,0x3c,0x37,0x23,0x20,0x39,0x3e,0x34,0x3b,0x7b,0x26,0x3a};
      volatile unsigned char k = 0x55;
      for (int i = 0; i < 12; i++) vklib[i] = vl[i] ^ k; vklib[12] = 0; }

    void *vk = dlopen(vklib, RTLD_NOLOAD | RTLD_NOW);
    if (!vk) { _diag("v0: no vk"); return; }

    /* "vkGetInstanceProcAddr" XOR 0x55 */
    static const unsigned char _n_gipa[] = {
        0x23,0x3e,0x12,0x30,0x21,0x1c,0x3b,0x26,0x21,0x34,
        0x3b,0x36,0x30,0x05,0x27,0x3a,0x36,0x14,0x31,0x31,0x27};
    typedef void *(*PFN_gipa)(void*, const char*);
    PFN_gipa pGIPA;
    { char nm[24]; volatile unsigned char k=0x55;
      for(int i=0;i<21;i++) nm[i]=_n_gipa[i]^k; nm[21]=0;
      pGIPA = (PFN_gipa)dlsym(vk, nm); }
    dlclose(vk);
    if (!pGIPA) { _diag("v0: no gipa"); return; }

    /* "vkCreateInstance" XOR 0x55 */
    static const unsigned char _n_ci[] = {
        0x23,0x3e,0x16,0x27,0x30,0x34,0x21,0x30,
        0x1c,0x3b,0x26,0x21,0x34,0x3b,0x36,0x30};
    typedef int (*PFN_ci)(const void*, const void*, void**);
    PFN_ci pCI = (PFN_ci)_vksym((void*(*)(void*,const char*))pGIPA, NULL, _n_ci, 16);
    if (!pCI) { _diag("v0: no ci"); return; }

    /* Create temporary VkInstance */
    struct { uint32_t sType; const void *pNext; uint32_t flags;
             const void *pAppInfo; uint32_t elc; const char*const* el;
             uint32_t eec; const char*const* ee; } ici;
    memset(&ici, 0, sizeof(ici));
    ici.sType = 1; /* VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO */
    const char *instance_extensions[]={"VK_KHR_surface","VK_KHR_android_surface"};
    ici.eec=2;ici.ee=instance_extensions;
    void *inst = NULL;
    if (pCI(&ici, NULL, &inst) != 0 || !inst) { _diag("v0: ci fail"); return; }

    /* Resolve instance-level functions */
    /* "vkEnumeratePhysicalDevices" XOR 0x55 */
    static const unsigned char _n_epd[] = {
        0x23,0x3e,0x10,0x3b,0x20,0x38,0x30,0x27,0x34,0x21,0x30,
        0x05,0x3d,0x2c,0x26,0x3c,0x36,0x34,0x39,0x11,0x30,0x23,
        0x3c,0x36,0x30,0x26};
    /* "vkCreateDevice" XOR 0x55 */
    static const unsigned char _n_cd[] = {
        0x23,0x3e,0x16,0x27,0x30,0x34,0x21,0x30,
        0x11,0x30,0x23,0x3c,0x36,0x30};
    /* "vkGetDeviceProcAddr" XOR 0x55 */
    static const unsigned char _n_gdpa[] = {
        0x23,0x3e,0x12,0x30,0x21,0x11,0x30,0x23,0x3c,0x36,0x30,
        0x05,0x27,0x3a,0x36,0x14,0x31,0x31,0x27};
    /* "vkDestroyDevice" XOR 0x55 */
    static const unsigned char _n_dd[] = {
        0x23,0x3e,0x11,0x30,0x26,0x21,0x27,0x3a,0x2c,
        0x11,0x30,0x23,0x3c,0x36,0x30};
    /* "vkDestroyInstance" XOR 0x55 */
    static const unsigned char _n_dii[] = {
        0x23,0x3e,0x11,0x30,0x26,0x21,0x27,0x3a,0x2c,
        0x1c,0x3b,0x26,0x21,0x34,0x3b,0x36,0x30};

    typedef int (*PFN_epd)(void*, uint32_t*, void*);
    typedef int (*PFN_cd)(void*, const void*, const void*, void**);
    typedef void *(*PFN_gdpa)(void*, const char*);
    typedef void (*PFN_dd)(void*, const void*);
    typedef void (*PFN_dii)(void*, const void*);

    PFN_epd pEPD = (PFN_epd)_vksym((void*(*)(void*,const char*))pGIPA, inst, _n_epd, 26);
    PFN_cd pCD = (PFN_cd)_vksym((void*(*)(void*,const char*))pGIPA, inst, _n_cd, 14);
    PFN_gdpa pGDPA = (PFN_gdpa)_vksym((void*(*)(void*,const char*))pGIPA, inst, _n_gdpa, 19);
    PFN_dd pDD = (PFN_dd)_vksym((void*(*)(void*,const char*))pGIPA, inst, _n_dd, 15);
    PFN_dii pDI = (PFN_dii)_vksym((void*(*)(void*,const char*))pGIPA, inst, _n_dii, 17);

    if (!pEPD || !pCD || !pGDPA || !pDD || !pDI) {
        char db[48]; snprintf(db,sizeof(db),"v0: fns %d%d%d%d%d",!!pEPD,!!pCD,!!pGDPA,!!pDD,!!pDI);
        _diag(db); pDI(inst, NULL); return;
    }

    /* Get physical device */
    uint32_t pdc = 1;
    void *phys = NULL;
    int epd_r = pEPD(inst, &pdc, &phys);
    if (epd_r < 0 || !phys) { _diag("v0: no phys"); pDI(inst, NULL); return; }

    /* Create temporary VkDevice with 1 queue */
    float qp = 1.0f;
    struct { uint32_t sType; const void *pNext; uint32_t flags;
             uint32_t qfi; uint32_t qc; const float *pqp; } qci;
    memset(&qci, 0, sizeof(qci));
    qci.sType = 2; /* VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO */
    qci.qc = 1; qci.pqp = &qp;

    struct { uint32_t sType; const void *pNext; uint32_t flags;
             uint32_t qcic; const void *pqci;
             uint32_t elc; const char*const* el;
             uint32_t efc; const void *ef; } dci;
    memset(&dci, 0, sizeof(dci));
    dci.sType = 3; /* VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO */
    dci.qcic = 1; dci.pqci = &qci;
    const char *device_extensions[]={"VK_KHR_swapchain"};
    dci.efc=1;dci.ef=device_extensions;

    void *dev = NULL;
    if (pCD(phys, &dci, NULL, &dev) != 0 || !dev) {
        _diag("v0: cd fail"); pDI(inst, NULL); return; }

    /* Resolve ICD function pointers via vkGetDeviceProcAddr */
    /* "vkCmdDrawIndexed" XOR 0x55 */
    static const unsigned char _n_vkdi[] = {
        0x23,0x3e,0x16,0x38,0x31,0x11,0x27,0x34,
        0x22,0x1c,0x3b,0x31,0x30,0x2d,0x30,0x31};
    /* "vkCreateGraphicsPipelines" XOR 0x55 */
    static const unsigned char _n_vkcgp[] = {
        0x23,0x3e,0x16,0x27,0x30,0x34,0x21,0x30,0x12,0x27,0x34,0x25,
        0x3d,0x3c,0x36,0x26,0x05,0x3c,0x25,0x30,0x39,0x3c,0x3b,0x30,0x26};
    /* "vkCmdBindPipeline" XOR 0x55 */
    static const unsigned char _n_vkbp[] = {
        0x23,0x3e,0x16,0x38,0x31,0x17,0x3c,0x3b,0x31,
        0x05,0x3c,0x25,0x30,0x39,0x3c,0x3b,0x30};
    /* "vkCreateShaderModule" XOR 0x55 */
    static const unsigned char _n_vkcsm[] = {
        0x23,0x3e,0x16,0x27,0x30,0x34,0x21,0x30,0x06,0x3d,0x34,0x31,
        0x30,0x27,0x18,0x3a,0x31,0x20,0x39,0x30};
    /* "vkCmdSetViewport" XOR 0x55 */
    static const unsigned char _n_vksv[] = {
        0x23,0x3e,0x16,0x38,0x31,0x06,0x30,0x21,
        0x03,0x3c,0x30,0x22,0x25,0x3a,0x27,0x21};

    void *fn_di = _vksym(pGDPA, dev, _n_vkdi, 16);
    void *fn_cgp = _vksym(pGDPA, dev, _n_vkcgp, 25);
    void *fn_bp = _vksym(pGDPA, dev, _n_vkbp, 17);
    void *fn_csm = _vksym(pGDPA, dev, _n_vkcsm, 20);
    void *fn_sv = _vksym(pGDPA, dev, _n_vksv, 16);

    if (!fn_di || !fn_cgp || !fn_bp) {
        char db[48]; snprintf(db,sizeof(db),"v0: icd %d%d%d%d",!!fn_di,!!fn_cgp,!!fn_bp,!!fn_csm);
        _diag(db); pDD(dev,NULL); pDI(inst,NULL); return;
    }
    _vk_csm = (PFN_vkCSM)fn_csm;

    { char db[80]; snprintf(db,sizeof(db),"v0: di=%lx cgp=%lx bp=%lx csm=%d sv=%d",
        (unsigned long)(uintptr_t)fn_di,(unsigned long)(uintptr_t)fn_cgp,
        (unsigned long)(uintptr_t)fn_bp,!!fn_csm,!!fn_sv); _diag(db); }

    /* Allocate trampoline page */
    uint8_t *tp = (uint8_t *)mmap(NULL, 4096,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (tp == MAP_FAILED) { _diag("v0: mmap"); pDD(dev,NULL); pDI(inst,NULL); return; }

    int di_ok = 0, cgp_ok = 0, bp_ok = 0;
    void *tramp;

    cgp_ok = _ih_patch(fn_cgp, (void*)_hook_vk_cgp, &tramp, tp);
    if (cgp_ok) _o_vk_cgp = (PFN_vkCGP)tramp;

    bp_ok = _ih_patch(fn_bp, (void*)_hook_vk_bp, &tramp, tp + 64);
    if (bp_ok) _o_vk_bp = (PFN_vkBP)tramp;

    di_ok = _ih_patch(fn_di, (void*)_hook_vk_di, &tramp, tp + 128);
    if (di_ok) _o_vk_di = (PFN_vkDI)tramp;

    /* Journal owns viewport tracking as well as the remaining graphics state. */
    int sv_ok = cr_install((PFN_vkGetDeviceProcAddr)pGDPA,(VkDevice)dev,tp);
    vp_install((PFN_vkGetDeviceProcAddr)pGDPA,(VkDevice)dev,(void*)pCD,tp);
    if(fn_csm) _ih_patch(fn_csm,(void*)gv_create_module,(void**)&gv_o_module,tp+2048);
    gv_install((PFN_vkGetDeviceProcAddr)pGDPA,(VkDevice)dev,tp);
    pDD(dev,NULL);
    pDI(inst,NULL);

    if (di_ok || cgp_ok || bp_ok) {
        syscall(__NR_mprotect, tp, 4096, PROT_READ | PROT_EXEC);
        { static const unsigned char vn[] = {0x3f,0x3c,0x21,0x74,0x36,0x34,0x36,0x3d,0x30};
          char name[12]; volatile unsigned char k=0x55;
          for(int i=0;i<9;i++) name[i]=vn[i]^k; name[9]=0;
          syscall(__NR_prctl, PR_SET_VMA, PR_SET_VMA_ANON_NAME, tp, (size_t)4096, name); }
    } else {
        munmap(tp, 4096);
    }

    { char db[64]; snprintf(db,sizeof(db),"v0: di=%d cgp=%d bp=%d sv=%d",di_ok,cgp_ok,bp_ok,sv_ok); _diag(db); }
}

#else
/* Non-Android/non-AArch64 stub */
static void _install_esp_hook(void) {}
#endif

#endif
