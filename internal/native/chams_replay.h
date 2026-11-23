/* Ordered graphics-state journal for chams. No application resources are owned
 * here. Commands are replayed only in the subpass in which draws were captured.
 * Bounds failures suppress replay rather than issuing draws with partial state.
 * Vulkan 1.0 inline render passes only; secondary and unsupported state fail closed. */
#ifndef INTERNAL_CHAMS_REPLAY_H
#define INTERNAL_CHAMS_REPLAY_H
#include <vulkan/vulkan.h>
#define CR_CB_CAP 128
#define CR_EVENT_CAP 8192
static PFN_vkCmdBindDescriptorSets cr_o_vkCmdBindDescriptorSets;
static PFN_vkCmdBindVertexBuffers cr_o_vkCmdBindVertexBuffers;
static PFN_vkCmdBindIndexBuffer cr_o_vkCmdBindIndexBuffer;
static PFN_vkCmdPushConstants cr_o_vkCmdPushConstants;
static PFN_vkCmdSetViewport cr_o_vkCmdSetViewport;
static PFN_vkCmdSetScissor cr_o_vkCmdSetScissor;
static PFN_vkCmdSetLineWidth cr_o_vkCmdSetLineWidth;
static PFN_vkCmdSetDepthBias cr_o_vkCmdSetDepthBias;
static PFN_vkCmdSetBlendConstants cr_o_vkCmdSetBlendConstants;
static PFN_vkCmdSetDepthBounds cr_o_vkCmdSetDepthBounds;
static PFN_vkCmdSetStencilCompareMask cr_o_vkCmdSetStencilCompareMask;
static PFN_vkCmdSetStencilWriteMask cr_o_vkCmdSetStencilWriteMask;
static PFN_vkCmdSetStencilReference cr_o_vkCmdSetStencilReference;
static PFN_vkBeginCommandBuffer cr_o_begin;
static PFN_vkFreeCommandBuffers cr_o_free;
static PFN_vkCmdBeginRenderPass cr_o_begin_pass;
static PFN_vkCmdEndRenderPass cr_o_end_pass;
static PFN_vkCmdNextSubpass cr_o_next;

static _Atomic unsigned cr_flushes, cr_draws, cr_drops;
typedef struct {
    unsigned kind;
    union {
        struct { VkPipe_ pipe; } pipeline;
        struct { VkPipe_ inv, vis; uint32_t ic, inst, fi, fInst; int32_t vo; } draw;
        struct { VkPipelineBindPoint bp; VkPipelineLayout layout; uint32_t first; uint32_t count; uint32_t ndyn; VkDescriptorSet sets[8]; uint32_t dyn[32]; } c2;
        struct { uint32_t first; uint32_t count; VkBuffer buffers[16]; VkDeviceSize offsets[16]; } c3;
        struct { VkBuffer buffer; VkDeviceSize offset; VkIndexType type; } c4;
        struct { VkPipelineLayout layout; VkShaderStageFlags stages; uint32_t offset; uint32_t size; uint8_t values[256]; } c5;
        struct { uint32_t first; uint32_t count; VkViewport values[16]; } c6;
        struct { uint32_t first; uint32_t count; VkRect2D values[16]; } c7;
        struct { float width; } c8;
        struct { float constant; float clamp; float slope; } c9;
        struct { float values[4]; } c10;
        struct { float lo; float hi; } c11;
        struct { VkStencilFaceFlags face; uint32_t mask; } c12;
        struct { VkStencilFaceFlags face; uint32_t mask; } c13;
        struct { VkStencilFaceFlags face; uint32_t ref; } c14;
    } u;
} CrEvent;
typedef struct {
    VkCommandBuffer cmd;
    CrEvent *events;
    unsigned count, draws;
    int active, bad, secondary;
} CrBuffer;
static CrBuffer cr_buffers[CR_CB_CAP];
static pthread_mutex_t cr_mutex = PTHREAD_MUTEX_INITIALIZER;

/* Vulkan externally synchronizes recording of each individual command buffer.
 * The mutex protects registration/free, not recording on independent buffers. */
static CrBuffer *cr_get(VkCommandBuffer cmd, int create) {
    CrBuffer *found = NULL, *empty = NULL;
    pthread_mutex_lock(&cr_mutex);
    for (int i=0; i<CR_CB_CAP; i++) {
        if (cr_buffers[i].cmd == cmd) { found = &cr_buffers[i]; break; }
        if (!cr_buffers[i].cmd && !empty) empty = &cr_buffers[i];
    }
    if (!found && create && empty) { found = empty; found->cmd = cmd; }
    pthread_mutex_unlock(&cr_mutex);
    return found;
}
static CrEvent *cr_event(CrBuffer *b, unsigned kind) {
    if (!b || b->bad || b->secondary) return NULL;
    if (b->count == CR_EVENT_CAP) { b->bad=1; atomic_fetch_add(&cr_drops,1); return NULL; }
    if (!b->events) b->events = calloc(CR_EVENT_CAP, sizeof(CrEvent));
    if (!b->events) { b->bad=1; return NULL; }
    CrEvent *e = &b->events[b->count++]; e->kind=kind; return e;
}
static void cr_pipeline(VkCmdBuf_ cmd, VkPipe_ pipe) {
    CrEvent *e=cr_event(cr_get((VkCommandBuffer)cmd,0),0);
    if(e) e->u.pipeline.pipe=pipe;
}
static int cr_capture(VkCmdBuf_ cmd, VkPipe_ inv, VkPipe_ vis,
    uint32_t ic, uint32_t inst, uint32_t fi, int32_t vo, uint32_t fInst) {
    CrBuffer *b=cr_get((VkCommandBuffer)cmd,0);
    if (!cr_ready) return 0;
    if (!b || !b->active || b->secondary || b->bad) return 1;
    CrEvent *e=cr_event(b,1);
    if (!e) return 1;
    e->u.draw.inv=inv; e->u.draw.vis=vis;
    e->u.draw.ic=ic; e->u.draw.inst=inst; e->u.draw.fi=fi;
    e->u.draw.vo=vo; e->u.draw.fInst=fInst; b->draws++;
    return 1;
}
static void cr_color(VkCmdBuf_ cmd, int visible) {
    unsigned color=atomic_load(visible ? &_er_chams_vis : &_er_chams_inv);
    /* Zero is a valid black selection; defaults live in Java preferences. */
    int brightness=atomic_load(&_er_chams_brightness);
    if(brightness<10) brightness=10;
    if(brightness>100) brightness=100;
    float scale=brightness/100.0f;
    float values[]={((color>>16)&255)/255.0f*scale,
        ((color>>8)&255)/255.0f*scale,(color&255)/255.0f*scale,1.0f};
    if(cr_o_vkCmdSetBlendConstants) cr_o_vkCmdSetBlendConstants((VkCommandBuffer)cmd,values);
}
static void cr_flush(CrBuffer *b) {
    if (!b || !b->active || !b->draws || b->bad) return;
    /* All red silhouettes, then all green surfaces: self-occlusion and mesh
     * section order cannot leave red over a visible front surface. Scene depth
     * is complete by this point and remains read-only for both passes. */
    for (int pass=0;pass<2;pass++) for(unsigned i=0;i<b->count;i++) {
        CrEvent *e=&b->events[i];
        switch(e->kind) {
        case 0: _o_vk_bp((VkCmdBuf_)b->cmd,0,e->u.pipeline.pipe); break;
        case 1:
            _o_vk_bp((VkCmdBuf_)b->cmd,0,pass ? e->u.draw.vis : e->u.draw.inv);
            cr_color((VkCmdBuf_)b->cmd,pass);
            _o_vk_di((VkCmdBuf_)b->cmd,e->u.draw.ic,e->u.draw.inst,
                e->u.draw.fi,e->u.draw.vo,e->u.draw.fInst); break;
        case 2: cr_o_vkCmdBindDescriptorSets(b->cmd, e->u.c2.bp, e->u.c2.layout, e->u.c2.first, e->u.c2.count, e->u.c2.sets, e->u.c2.ndyn, e->u.c2.dyn); break;
        case 3: cr_o_vkCmdBindVertexBuffers(b->cmd, e->u.c3.first, e->u.c3.count, e->u.c3.buffers, e->u.c3.offsets); break;
        case 4: cr_o_vkCmdBindIndexBuffer(b->cmd, e->u.c4.buffer, e->u.c4.offset, e->u.c4.type); break;
        case 5: cr_o_vkCmdPushConstants(b->cmd, e->u.c5.layout, e->u.c5.stages, e->u.c5.offset, e->u.c5.size, e->u.c5.values); break;
        case 6: cr_o_vkCmdSetViewport(b->cmd, e->u.c6.first, e->u.c6.count, e->u.c6.values); break;
        case 7: cr_o_vkCmdSetScissor(b->cmd, e->u.c7.first, e->u.c7.count, e->u.c7.values); break;
        case 8: cr_o_vkCmdSetLineWidth(b->cmd, e->u.c8.width); break;
        case 9: cr_o_vkCmdSetDepthBias(b->cmd, e->u.c9.constant, e->u.c9.clamp, e->u.c9.slope); break;
        case 10: cr_o_vkCmdSetBlendConstants(b->cmd, e->u.c10.values); break;
        case 11: cr_o_vkCmdSetDepthBounds(b->cmd, e->u.c11.lo, e->u.c11.hi); break;
        case 12: cr_o_vkCmdSetStencilCompareMask(b->cmd, e->u.c12.face, e->u.c12.mask); break;
        case 13: cr_o_vkCmdSetStencilWriteMask(b->cmd, e->u.c13.face, e->u.c13.mask); break;
        case 14: cr_o_vkCmdSetStencilReference(b->cmd, e->u.c14.face, e->u.c14.ref); break;
        }
    }
    /* Restore the host pipeline; the last replay leaves all other state at the
     * end-of-subpass value. Keep state events for inherited state in later passes. */
    _o_vk_bp((VkCmdBuf_)b->cmd,0,_vk_cur((VkCmdBuf_)b->cmd));
    for(unsigned i=b->count;i>0;i--) if(b->events[i-1].kind==10) {
        cr_o_vkCmdSetBlendConstants(b->cmd,b->events[i-1].u.c10.values); break;
    }
    atomic_fetch_add(&cr_flushes,1); atomic_fetch_add(&cr_draws,b->draws);
    unsigned dst=0;
    for(unsigned i=0;i<b->count;i++) if(b->events[i].kind!=1)
        b->events[dst++]=b->events[i];
    b->count=dst; b->draws=0;
}
static VkResult cr_begin(VkCommandBuffer cmd,const VkCommandBufferBeginInfo *info) {
    VkResult r=cr_o_begin(cmd,info);
    if(r==VK_SUCCESS) {
        gv_cmd_reset(cmd);
        CrBuffer *b=cr_get(cmd,1);
        if(b) { b->count=b->draws=0; b->active=b->bad=0;
            b->secondary=!!(info->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT); }
        _vk_set((VkCmdBuf_)cmd,NULL);
    }
    return r;
}
static void cr_free(VkDevice dev,VkCommandPool pool,uint32_t count,const VkCommandBuffer *cmds) {
    for(uint32_t i=0;i<count;i++) {
        gv_cmd_free(cmds[i]);
        CrBuffer *b=cr_get(cmds[i],0);
        if(b) { pthread_mutex_lock(&cr_mutex); free(b->events); memset(b,0,sizeof(*b)); pthread_mutex_unlock(&cr_mutex); }
    }
    cr_o_free(dev,pool,count,cmds);
}
static void cr_begin_pass(VkCommandBuffer cmd,const VkRenderPassBeginInfo *info,VkSubpassContents contents) {
    gv_begin_pass(cmd,info);
    CrBuffer *b=cr_get(cmd,0);
    if(b) { b->active=(contents==VK_SUBPASS_CONTENTS_INLINE); b->draws=0; }
    cr_o_begin_pass(cmd,info,contents);
}
static void cr_end_pass(VkCommandBuffer cmd) {
    CrBuffer *b=cr_get(cmd,0); cr_flush(b); if(b) b->active=0;
    cr_o_end_pass(cmd);
}
static void cr_next(VkCommandBuffer cmd,VkSubpassContents contents) {
    CrBuffer *b=cr_get(cmd,0); cr_flush(b);
    if(b) b->active=(contents==VK_SUBPASS_CONTENTS_INLINE);
    cr_o_next(cmd,contents);
}
static void cr_vkCmdBindDescriptorSets(VkCommandBuffer cmd, VkPipelineBindPoint bp, VkPipelineLayout layout, uint32_t first, uint32_t count, const VkDescriptorSet *sets, uint32_t ndyn, const uint32_t *dyn) {
    CrBuffer *b=cr_get(cmd,0);
    if (bp == VK_PIPELINE_BIND_POINT_GRAPHICS) {
        if (b && (count > 8 || ndyn > 32)) b->bad=1;
        CrEvent *event=cr_event(b,2);
        if(event) {
            __typeof__(event->u.c2) *e=&event->u.c2;
            e->bp=bp; e->layout=layout; e->first=first; e->count=count; e->ndyn=ndyn; memcpy(e->sets, sets, count * sizeof(*sets)); if (ndyn) memcpy(e->dyn, dyn, ndyn * sizeof(*dyn));
        }
    }
    cr_o_vkCmdBindDescriptorSets(cmd, bp, layout, first, count, sets, ndyn, dyn);
}
static void cr_vkCmdBindVertexBuffers(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkBuffer *buffers, const VkDeviceSize *offsets) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (count > 16)) b->bad=1;
        CrEvent *event=cr_event(b,3);
        if(event) {
            __typeof__(event->u.c3) *e=&event->u.c3;
            e->first=first; e->count=count; memcpy(e->buffers, buffers, count * sizeof(*buffers)); memcpy(e->offsets, offsets, count * sizeof(*offsets));
        }
    }
    cr_o_vkCmdBindVertexBuffers(cmd, first, count, buffers, offsets);
}
static void cr_vkCmdBindIndexBuffer(VkCommandBuffer cmd, VkBuffer buffer, VkDeviceSize offset, VkIndexType type) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (0)) b->bad=1;
        CrEvent *event=cr_event(b,4);
        if(event) {
            __typeof__(event->u.c4) *e=&event->u.c4;
            e->buffer=buffer; e->offset=offset; e->type=type; 
        }
    }
    cr_o_vkCmdBindIndexBuffer(cmd, buffer, offset, type);
}
static void cr_vkCmdPushConstants(VkCommandBuffer cmd, VkPipelineLayout layout, VkShaderStageFlags stages, uint32_t offset, uint32_t size, const void *values) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (size > 256)) b->bad=1;
        CrEvent *event=cr_event(b,5);
        if(event) {
            __typeof__(event->u.c5) *e=&event->u.c5;
            e->layout=layout; e->stages=stages; e->offset=offset; e->size=size; memcpy(e->values, values, size);
        }
    }
    cr_o_vkCmdPushConstants(cmd, layout, stages, offset, size, values);
}
static void cr_vkCmdSetViewport(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkViewport *values) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (count > 16)) b->bad=1;
        CrEvent *event=cr_event(b,6);
        if(event) {
            __typeof__(event->u.c6) *e=&event->u.c6;
            e->first=first; e->count=count; memcpy(e->values, values, count * sizeof(*values));
        }
    }
    cr_o_vkCmdSetViewport(cmd, first, count, values);
}
static void cr_vkCmdSetScissor(VkCommandBuffer cmd, uint32_t first, uint32_t count, const VkRect2D *values) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (count > 16)) b->bad=1;
        CrEvent *event=cr_event(b,7);
        if(event) {
            __typeof__(event->u.c7) *e=&event->u.c7;
            e->first=first; e->count=count; memcpy(e->values, values, count * sizeof(*values));
        }
    }
    cr_o_vkCmdSetScissor(cmd, first, count, values);
}
static void cr_vkCmdSetLineWidth(VkCommandBuffer cmd, float width) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (0)) b->bad=1;
        CrEvent *event=cr_event(b,8);
        if(event) {
            __typeof__(event->u.c8) *e=&event->u.c8;
            e->width=width; 
        }
    }
    cr_o_vkCmdSetLineWidth(cmd, width);
}
static void cr_vkCmdSetDepthBias(VkCommandBuffer cmd, float constant, float clamp, float slope) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (0)) b->bad=1;
        CrEvent *event=cr_event(b,9);
        if(event) {
            __typeof__(event->u.c9) *e=&event->u.c9;
            e->constant=constant; e->clamp=clamp; e->slope=slope; 
        }
    }
    cr_o_vkCmdSetDepthBias(cmd, constant, clamp, slope);
}
static void cr_vkCmdSetBlendConstants(VkCommandBuffer cmd, const float values[4]) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (0)) b->bad=1;
        CrEvent *event=cr_event(b,10);
        if(event) {
            __typeof__(event->u.c10) *e=&event->u.c10;
             memcpy(e->values, values, sizeof(e->values));
        }
    }
    cr_o_vkCmdSetBlendConstants(cmd, values);
}
static void cr_vkCmdSetDepthBounds(VkCommandBuffer cmd, float lo, float hi) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (0)) b->bad=1;
        CrEvent *event=cr_event(b,11);
        if(event) {
            __typeof__(event->u.c11) *e=&event->u.c11;
            e->lo=lo; e->hi=hi; 
        }
    }
    cr_o_vkCmdSetDepthBounds(cmd, lo, hi);
}
static void cr_vkCmdSetStencilCompareMask(VkCommandBuffer cmd, VkStencilFaceFlags face, uint32_t mask) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (0)) b->bad=1;
        CrEvent *event=cr_event(b,12);
        if(event) {
            __typeof__(event->u.c12) *e=&event->u.c12;
            e->face=face; e->mask=mask; 
        }
    }
    cr_o_vkCmdSetStencilCompareMask(cmd, face, mask);
}
static void cr_vkCmdSetStencilWriteMask(VkCommandBuffer cmd, VkStencilFaceFlags face, uint32_t mask) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (0)) b->bad=1;
        CrEvent *event=cr_event(b,13);
        if(event) {
            __typeof__(event->u.c13) *e=&event->u.c13;
            e->face=face; e->mask=mask; 
        }
    }
    cr_o_vkCmdSetStencilWriteMask(cmd, face, mask);
}
static void cr_vkCmdSetStencilReference(VkCommandBuffer cmd, VkStencilFaceFlags face, uint32_t ref) {
    CrBuffer *b=cr_get(cmd,0);
    if (1) {
        if (b && (0)) b->bad=1;
        CrEvent *event=cr_event(b,14);
        if(event) {
            __typeof__(event->u.c14) *e=&event->u.c14;
            e->face=face; e->ref=ref; 
        }
    }
    cr_o_vkCmdSetStencilReference(cmd, face, ref);
}
static void cr_log(void) {
    gv_log();
    char db[120]; snprintf(db,sizeof(db),"v2: flush=%u draws=%u drops=%u ready=%d",
        atomic_load(&cr_flushes),atomic_load(&cr_draws),atomic_load(&cr_drops),cr_ready); _diag(db);
}
static int cr_install(PFN_vkGetDeviceProcAddr get,VkDevice dev,uint8_t *tp) {
    struct { const char *name; void *hook; void **original; } hooks[] = {
        {"vkBeginCommandBuffer",(void*)cr_begin,(void**)&cr_o_begin},
        {"vkFreeCommandBuffers",(void*)cr_free,(void**)&cr_o_free},
        {"vkCmdBeginRenderPass",(void*)cr_begin_pass,(void**)&cr_o_begin_pass},
        {"vkCmdEndRenderPass",(void*)cr_end_pass,(void**)&cr_o_end_pass},
        {"vkCmdNextSubpass",(void*)cr_next,(void**)&cr_o_next},
        {"vkCmdBindDescriptorSets",(void*)cr_vkCmdBindDescriptorSets,(void**)&cr_o_vkCmdBindDescriptorSets},
        {"vkCmdBindVertexBuffers",(void*)cr_vkCmdBindVertexBuffers,(void**)&cr_o_vkCmdBindVertexBuffers},
        {"vkCmdBindIndexBuffer",(void*)cr_vkCmdBindIndexBuffer,(void**)&cr_o_vkCmdBindIndexBuffer},
        {"vkCmdPushConstants",(void*)cr_vkCmdPushConstants,(void**)&cr_o_vkCmdPushConstants},
        {"vkCmdSetViewport",(void*)cr_vkCmdSetViewport,(void**)&cr_o_vkCmdSetViewport},
        {"vkCmdSetScissor",(void*)cr_vkCmdSetScissor,(void**)&cr_o_vkCmdSetScissor},
        {"vkCmdSetLineWidth",(void*)cr_vkCmdSetLineWidth,(void**)&cr_o_vkCmdSetLineWidth},
        {"vkCmdSetDepthBias",(void*)cr_vkCmdSetDepthBias,(void**)&cr_o_vkCmdSetDepthBias},
        {"vkCmdSetBlendConstants",(void*)cr_vkCmdSetBlendConstants,(void**)&cr_o_vkCmdSetBlendConstants},
        {"vkCmdSetDepthBounds",(void*)cr_vkCmdSetDepthBounds,(void**)&cr_o_vkCmdSetDepthBounds},
        {"vkCmdSetStencilCompareMask",(void*)cr_vkCmdSetStencilCompareMask,(void**)&cr_o_vkCmdSetStencilCompareMask},
        {"vkCmdSetStencilWriteMask",(void*)cr_vkCmdSetStencilWriteMask,(void**)&cr_o_vkCmdSetStencilWriteMask},
        {"vkCmdSetStencilReference",(void*)cr_vkCmdSetStencilReference,(void**)&cr_o_vkCmdSetStencilReference},
    };
    void *functions[sizeof(hooks)/sizeof(hooks[0])];
    for(unsigned i=0;i<sizeof(hooks)/sizeof(hooks[0]);i++) {
        functions[i]=(void*)get(dev,hooks[i].name);
        if(!functions[i]) return 0;
        uint32_t relocated[16];
        if(!cr_trampoline(relocated,(const uint32_t*)functions[i],(uintptr_t)functions[i])) {
            _diag("v2: journal prologue rejected"); return 0;
        }
    }
    unsigned installed=0;
    for(unsigned i=0;i<sizeof(hooks)/sizeof(hooks[0]);i++)
        installed+=_ih_patch(functions[i],hooks[i].hook,hooks[i].original,tp+256+i*64);
    cr_ready=(installed==sizeof(hooks)/sizeof(hooks[0]));
    char db[80]; snprintf(db,sizeof(db),"v2: journal hooks=%u ready=%d",installed,cr_ready); _diag(db);
    return cr_ready;
}
#endif
