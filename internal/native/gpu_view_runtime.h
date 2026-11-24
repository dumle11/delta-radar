#ifndef INTERNAL_GPU_VIEW_RUNTIME_H
#define INTERNAL_GPU_VIEW_RUNTIME_H
/* Follow Vulkan's owned mapped uniform buffers, copy the matrix at the original
 * skeletal draw, and carry that copy through submit signal semaphores to present.
 * Never call engine camera methods or dereference arbitrary game-object fields. */
#define GV_MEMORY_CAP 2048
#define GV_BUFFER_CAP 8192
#define GV_SET_CAP 8192
static struct {unsigned used;VkDeviceMemory handle;VkDeviceSize size,offset,mapped_size;void *pointer;} gv_memory[GV_MEMORY_CAP];
static struct {unsigned used;VkBuffer handle;VkDeviceMemory memory;VkDeviceSize offset;} gv_buffers[GV_BUFFER_CAP];
static struct {unsigned used;VkDescriptorSet handle;VkDescriptorBufferInfo buffers[2];VkDescriptorType types[2];int valid[2];} gv_sets[GV_SET_CAP];
static struct {VkCommandBuffer command;GvFrame view;uint32_t width,height;} gv_commands[CR_CB_CAP];
static struct {VkSemaphore semaphore;GvFrame view;} gv_signals[512];
static struct {VkQueue queue;GvFrame view;} gv_queues[16];
static unsigned gv_serial;
static _Atomic unsigned gv_captured,gv_missing,gv_reads_failed,gv_ubo_writes,gv_maps;
static _Atomic int gv_stage;
static unsigned gv_read_rejects[7];
static _Thread_local int gv_internal_submit;
static PFN_vkAllocateMemory gv_o_allocate;
static PFN_vkFreeMemory gv_o_free_memory;
typedef VkResult (*GvMapMemory)(VkDeviceMemory,VkDeviceSize,VkDeviceSize,void**);
typedef VkResult (*GvUnmapMemory)(VkDeviceMemory);
static GvMapMemory gv_o_map_impl;
static GvUnmapMemory gv_o_unmap_impl;
static PFN_vkBindBufferMemory gv_o_bind;
static PFN_vkUpdateDescriptorSets gv_o_update;
static PFN_vkQueueSubmit gv_o_submit;
static unsigned gv_hash(uint64_t handle,unsigned cap) {handle^=handle>>23;return (unsigned)((handle>>4)^(handle>>13))&(cap-1);}
/* Linear probing with tombstones preserves colliding live resources. A full
 * table rejects a new entry instead of evicting an unrelated live allocation. */
#define GV_LOOKUP(name,table,cap,type) \
static int name(type handle,int create) { \
    unsigned start=gv_hash((uint64_t)handle,cap);int retired=-1; \
    for(unsigned i=0;i<cap;i++) {unsigned slot=(start+i)&(cap-1); \
        if(table[slot].used==1 && table[slot].handle==handle) return (int)slot; \
        if(table[slot].used==2 && retired<0) retired=(int)slot; \
        if(!table[slot].used) {if(!create) return -1;return retired>=0?retired:(int)slot;} \
    } \
    return create?retired:-1; \
}
GV_LOOKUP(gv_memory_slot,gv_memory,GV_MEMORY_CAP,VkDeviceMemory)
GV_LOOKUP(gv_buffer_slot,gv_buffers,GV_BUFFER_CAP,VkBuffer)
GV_LOOKUP(gv_set_slot,gv_sets,GV_SET_CAP,VkDescriptorSet)
#undef GV_LOOKUP
static VkResult gv_allocate(VkDevice device,const VkMemoryAllocateInfo *info,const VkAllocationCallbacks *alloc,VkDeviceMemory *memory) {
    VkResult r=gv_o_allocate(device,info,alloc,memory);
    if(r==VK_SUCCESS) {
        pthread_mutex_lock(&gv_mutex);int slot=gv_memory_slot(*memory,1);
        if(slot>=0) {
        memset(&gv_memory[slot],0,sizeof(gv_memory[slot]));gv_memory[slot].handle=*memory;gv_memory[slot].size=info->allocationSize;gv_memory[slot].used=1;}
        pthread_mutex_unlock(&gv_mutex);
    }
    return r;
}
static void gv_free_memory(VkDevice device,VkDeviceMemory memory,const VkAllocationCallbacks *alloc) {
    pthread_mutex_lock(&gv_mutex);int slot=gv_memory_slot(memory,0);
    if(slot>=0) {gv_memory[slot].pointer=NULL;gv_memory[slot].used=2;
        for(unsigned i=0;i<GV_BUFFER_CAP;i++) if(gv_buffers[i].used==1 && gv_buffers[i].memory==memory) gv_buffers[i].used=2;
    }
    pthread_mutex_unlock(&gv_mutex);gv_o_free_memory(device,memory,alloc);
}
static VkResult gv_map_impl(VkDeviceMemory memory,VkDeviceSize offset,VkDeviceSize size,void **data) {
    VkResult r=gv_o_map_impl(memory,offset,size,data);
    if(r==VK_SUCCESS) {
        atomic_fetch_add(&gv_maps,1);
        pthread_mutex_lock(&gv_mutex);int slot=gv_memory_slot(memory,0);
        if(slot>=0 && offset<=gv_memory[slot].size) {
            VkDeviceSize remaining=gv_memory[slot].size-offset;
            if(size==VK_WHOLE_SIZE) size=remaining;
            if(size<=remaining) {gv_memory[slot].offset=offset;gv_memory[slot].mapped_size=size;gv_memory[slot].pointer=*data;}
        }
        pthread_mutex_unlock(&gv_mutex);
    }
    return r;
}
static VkResult gv_unmap_impl(VkDeviceMemory memory) {
    pthread_mutex_lock(&gv_mutex);int slot=gv_memory_slot(memory,0);
    if(slot>=0) gv_memory[slot].pointer=NULL;
    pthread_mutex_unlock(&gv_mutex);return gv_o_unmap_impl(memory);
}
static VkResult gv_bind(VkDevice device,VkBuffer buffer,VkDeviceMemory memory,VkDeviceSize offset) {
    VkResult r=gv_o_bind(device,buffer,memory,offset);
    if(r==VK_SUCCESS) {
        pthread_mutex_lock(&gv_mutex);int slot=gv_buffer_slot(buffer,1);
        if(slot>=0) {
        gv_buffers[slot].handle=buffer;gv_buffers[slot].memory=memory;gv_buffers[slot].offset=offset;gv_buffers[slot].used=1;}
        pthread_mutex_unlock(&gv_mutex);
    }
    return r;
}
static void gv_update(VkDevice device,uint32_t count,const VkWriteDescriptorSet *writes,uint32_t copies,const VkCopyDescriptorSet *copy) {
    gv_o_update(device,count,writes,copies,copy);
    pthread_mutex_lock(&gv_mutex);
    for(uint32_t i=0;i<count;i++) {
        const VkWriteDescriptorSet *w=&writes[i];
        if(w->dstBinding>1 || w->dstArrayElement || w->descriptorCount!=1 || !w->pBufferInfo ||
           (w->descriptorType!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && w->descriptorType!=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)) continue;
        atomic_fetch_add(&gv_ubo_writes,1);
        int slot=gv_set_slot(w->dstSet,1);if(slot<0) continue;
        if(gv_sets[slot].used!=1 || gv_sets[slot].handle!=w->dstSet) {memset(&gv_sets[slot],0,sizeof(gv_sets[slot]));gv_sets[slot].handle=w->dstSet;gv_sets[slot].used=1;}
        gv_sets[slot].buffers[w->dstBinding]=w->pBufferInfo[0];gv_sets[slot].types[w->dstBinding]=w->descriptorType;gv_sets[slot].valid[w->dstBinding]=1;
    }
    for(uint32_t i=0;i<copies;i++) {
        const VkCopyDescriptorSet *c=&copy[i];
        if(c->descriptorCount!=1 || c->srcBinding>1 || c->dstBinding>1 || c->srcArrayElement || c->dstArrayElement) continue;
        int src=gv_set_slot(c->srcSet,0),dst=gv_set_slot(c->dstSet,1);
        if(src<0 || dst<0 || !gv_sets[src].valid[c->srcBinding]) continue;
        if(gv_sets[dst].used!=1 || gv_sets[dst].handle!=c->dstSet) {memset(&gv_sets[dst],0,sizeof(gv_sets[dst]));gv_sets[dst].handle=c->dstSet;gv_sets[dst].used=1;}
        gv_sets[dst].buffers[c->dstBinding]=gv_sets[src].buffers[c->srcBinding];gv_sets[dst].types[c->dstBinding]=gv_sets[src].types[c->srcBinding];gv_sets[dst].valid[c->dstBinding]=1;
    }
    pthread_mutex_unlock(&gv_mutex);
}
/* Called with gv_mutex held, including across the checked copy, so an unmap
 * cannot retire the mapped range while it is being sampled. */
static int gv_uniform_read(VkDescriptorBufferInfo info,uint32_t dynamic,void *out,size_t size) {
    int b=gv_buffer_slot(info.buffer,0);
    if(b<0) {gv_read_rejects[0]++;return 0;}
    if(info.offset>UINT64_MAX-dynamic) {gv_read_rejects[1]++;return 0;}
    VkDeviceSize offset=info.offset+dynamic;
    if(info.range!=VK_WHOLE_SIZE && size>info.range) {gv_read_rejects[1]++;return 0;}
    if(offset>UINT64_MAX-gv_buffers[b].offset) {gv_read_rejects[1]++;return 0;}
    offset+=gv_buffers[b].offset;
    VkDeviceMemory memory=gv_buffers[b].memory;int m=gv_memory_slot(memory,0);
    if(m<0) {gv_read_rejects[2]++;return 0;}
    if(!gv_memory[m].pointer) {gv_read_rejects[3]++;return 0;}
    if(offset<gv_memory[m].offset) {gv_read_rejects[4]++;return 0;}
    VkDeviceSize relative=offset-gv_memory[m].offset;
    if(relative>gv_memory[m].mapped_size || size>gv_memory[m].mapped_size-relative) {gv_read_rejects[4]++;return 0;}
    if(!ev_read((uintptr_t)gv_memory[m].pointer+(uintptr_t)relative,out,size)) {gv_read_rejects[5]++;gv_read_rejects[6]=errno;return 0;}
    return 1;
}
static int gv_cmd_slot(VkCommandBuffer command,int create) {
    int empty=-1;
    for(int i=0;i<CR_CB_CAP;i++) {if(gv_commands[i].command==command) return i;if(!gv_commands[i].command && empty<0) empty=i;}
    if(create && empty>=0) {gv_commands[empty].command=command;return empty;}return -1;
}
static void gv_cmd_reset(VkCommandBuffer command) {
    pthread_mutex_lock(&gv_mutex);int slot=gv_cmd_slot(command,1);
    if(slot>=0) {memset(&gv_commands[slot].view,0,sizeof(GvFrame));gv_commands[slot].width=gv_commands[slot].height=0;}
    pthread_mutex_unlock(&gv_mutex);
}
static void gv_cmd_free(VkCommandBuffer command) {
    pthread_mutex_lock(&gv_mutex);int slot=gv_cmd_slot(command,0);if(slot>=0) memset(&gv_commands[slot],0,sizeof(gv_commands[slot]));pthread_mutex_unlock(&gv_mutex);
}
static void gv_begin_pass(VkCommandBuffer command,const VkRenderPassBeginInfo *info) {
    pthread_mutex_lock(&gv_mutex);int slot=gv_cmd_slot(command,0);
    if(slot>=0) {gv_commands[slot].width=info->renderArea.offset.x+info->renderArea.extent.width;gv_commands[slot].height=info->renderArea.offset.y+info->renderArea.extent.height;}
    pthread_mutex_unlock(&gv_mutex);
}
static void gv_draw_view(VkCommandBuffer command,VkPipeline pipeline) {
    CrBuffer *cb=cr_get(command,0);if(!cb || !cb->active || cb->bad) return;
    pthread_mutex_lock(&gv_mutex);int slot=gv_cmd_slot(command,0);
    if(slot<0 || gv_commands[slot].view.valid) goto done;
    atomic_store(&gv_stage,1);
    GvProfile profile={0};
    for(unsigned i=0;i<128;i++) if(gv_pipelines[i].pipeline==pipeline) {profile=gv_pipelines[i].profile;break;}
    if(!profile.valid) goto done;
    atomic_store(&gv_stage,2);
    uint32_t width=gv_commands[slot].width,height=gv_commands[slot].height;
    if(width<180 || height<180 || (width<320 && height<320) || width>8192 || height>8192) goto done;
    atomic_store(&gv_stage,3);
    VkViewport viewport={0};const CrEvent *binding=NULL;
    for(unsigned i=cb->count;i>0;i--) {
        const CrEvent *e=&cb->events[i-1];
        if(!binding && e->kind==2 && e->u.c2.bp==VK_PIPELINE_BIND_POINT_GRAPHICS && e->u.c2.first==0 && e->u.c2.count>0) binding=e;
        if(e->kind==6 && e->u.c6.first==0 && e->u.c6.count>0 && viewport.width==0) viewport=e->u.c6.values[0];
        if(binding && viewport.width!=0) break;
    }
    if(!binding || viewport.width<320 || fabsf(viewport.height)<180) goto done;
    atomic_store(&gv_stage,4);
    int ds=gv_set_slot(binding->u.c2.sets[0],0);
    if(ds<0 || !gv_sets[ds].valid[0] || !gv_sets[ds].valid[1]) {atomic_fetch_add(&gv_missing,1);goto done;}
    atomic_store(&gv_stage,5);
    uint32_t offsets[2]={0};unsigned dynamic=0;
    for(unsigned b=0;b<2;b++) if(gv_sets[ds].types[b]==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
        if(dynamic>=binding->u.c2.ndyn) goto done;
        offsets[b]=binding->u.c2.dyn[dynamic++];
    }
    unsigned n=(unsigned)profile.translation+1;
    if(n>1024 || profile.matrix_a<0 || profile.matrix_b<0 || profile.local_origin<0 ||
       profile.matrix_a+4>(int)n || profile.matrix_b+4>(int)n || profile.local_origin>=(int)n) goto done;
    atomic_store(&gv_stage,6);
    float values[4096];uint32_t flags=0;
    if(!gv_uniform_read(gv_sets[ds].buffers[0],offsets[0],values,n*16) ||
       !gv_uniform_read(gv_sets[ds].buffers[1],offsets[1],&flags,4)) {atomic_fetch_add(&gv_reads_failed,1);goto done;}
    atomic_store(&gv_stage,7);
    GvFrame view={0};int base=(flags&1)?profile.matrix_a:profile.matrix_b;
    memcpy(view.matrix,&values[base*4],64);memcpy(view.translation,&values[profile.translation*4],12);
    for(unsigned i=0;i<16;i++) if(!isfinite(view.matrix[i]) || fabsf(view.matrix[i])>1e8f) goto done;
    float distance2=0;
    for(unsigned i=0;i<3;i++) {
        if(!isfinite(view.translation[i]) || fabsf(view.translation[i])>1e8f) goto done;
        float relative=values[profile.local_origin*4+i]+view.translation[i];
        if(!isfinite(relative)) goto done;distance2+=relative*relative;
    }
    atomic_store(&gv_stage,8);
    if(distance2<14400 || distance2>1e14f) goto done; /* reject near-camera FP view */
    atomic_store(&gv_stage,9);
    float wnorm=view.matrix[3]*view.matrix[3]+view.matrix[7]*view.matrix[7]+view.matrix[11]*view.matrix[11];
    if(wnorm<0.5f || wnorm>2.0f) goto done;
    view.viewport=viewport;view.width=width;view.height=height;view.recorded_at=ef_now();view.serial=++gv_serial;view.valid=1;
    gv_commands[slot].view=view;
    atomic_store(&gv_stage,0);
    {unsigned n=atomic_fetch_add(&gv_captured,1)+1;if(n==1 || n%300==0) {
        char b[180];snprintf(b,sizeof(b),"gpu view: captured=%u missing=%u read_fail=%u mat=%d pre=%d vp=%.0fx%.0f area=%ux%u flags=%x",n,
            atomic_load(&gv_missing),atomic_load(&gv_reads_failed),base,profile.translation,viewport.width,viewport.height,width,height,flags);_diag(b);}}
done:
    pthread_mutex_unlock(&gv_mutex);
}
static VkResult gv_submit(VkQueue queue,uint32_t count,const VkSubmitInfo *submits,VkFence fence) {
    if(gv_internal_submit) return gv_o_submit(queue,count,submits,fence);
    GvFrame view={0};struct {VkSemaphore semaphore;GvFrame view;} signals[64];unsigned signals_n=0;
    pthread_mutex_lock(&gv_mutex);int q=-1;
    for(unsigned i=0;i<16;i++) if(gv_queues[i].queue==queue || !gv_queues[i].queue) {q=(int)i;break;}
    if(q>=0) view=gv_queues[q].view;
    for(uint32_t i=0;i<count;i++) {
        for(uint32_t j=0;j<submits[i].commandBufferCount;j++) {
            int c=gv_cmd_slot(submits[i].pCommandBuffers[j],0);
            if(c>=0 && gv_commands[c].view.valid) view=gv_commands[c].view;
        }
        for(uint32_t j=0;j<submits[i].signalSemaphoreCount && signals_n<64;j++) {
            signals[signals_n].semaphore=submits[i].pSignalSemaphores[j];signals[signals_n++].view=view;
        }
    }
    pthread_mutex_unlock(&gv_mutex);
    VkResult r=gv_o_submit(queue,count,submits,fence);
    if(r==VK_SUCCESS) {
        pthread_mutex_lock(&gv_mutex);
        if(q>=0) {gv_queues[q].queue=queue;gv_queues[q].view=view;}
        for(unsigned i=0;i<signals_n;i++) {
            unsigned s=gv_hash((uint64_t)signals[i].semaphore,512);
            gv_signals[s].semaphore=signals[i].semaphore;gv_signals[s].view=signals[i].view;
        }
        pthread_mutex_unlock(&gv_mutex);
    }
    return r;
}
static int gv_present_view(VkQueue queue,const VkPresentInfoKHR *info,GvFrame *view) {
    memset(view,0,sizeof(*view));pthread_mutex_lock(&gv_mutex);
    for(uint32_t i=0;i<info->waitSemaphoreCount;i++) {
        unsigned s=gv_hash((uint64_t)info->pWaitSemaphores[i],512);
        if(gv_signals[s].semaphore==info->pWaitSemaphores[i] && gv_signals[s].view.valid && gv_signals[s].view.serial>view->serial) *view=gv_signals[s].view;
    }
    for(unsigned i=0;i<16;i++) if(gv_queues[i].queue==queue) {
        if(!info->waitSemaphoreCount) *view=gv_queues[i].view;
        gv_queues[i].view.valid=0; /* do not carry a previous-frame view into a frame with no scene */
        break;
    }
    pthread_mutex_unlock(&gv_mutex);
    if(!view->valid || ef_now()-view->recorded_at>200) {view->valid=0;return 0;}return 1;
}
static void gv_read_log(void);
static void gv_log(void) {
    char b[140];snprintf(b,sizeof(b),"gpu view: frames=%u missing=%u read_fail=%u writes=%u maps=%u stage=%d",atomic_load(&gv_captured),
        atomic_load(&gv_missing),atomic_load(&gv_reads_failed),atomic_load(&gv_ubo_writes),atomic_load(&gv_maps),atomic_load(&gv_stage));_diag(b);gv_read_log();
}
static void gv_read_log(void) {
    pthread_mutex_lock(&gv_mutex);
    char b[200];snprintf(b,sizeof(b),"gpu read rejects: buffer=%u range=%u memory=%u unmapped=%u bounds=%u copy=%u errno=%u",
        gv_read_rejects[0],gv_read_rejects[1],gv_read_rejects[2],gv_read_rejects[3],gv_read_rejects[4],gv_read_rejects[5],gv_read_rejects[6]);
    pthread_mutex_unlock(&gv_mutex);_diag(b);
}
static int gv_install(PFN_vkGetDeviceProcAddr get,VkDevice device,uint8_t *tp) {
    struct {const char *name;void *hook;void **original;void *target;} hooks[]={
        {"vkAllocateMemory",(void*)gv_allocate,(void**)&gv_o_allocate,NULL},
        {"vkFreeMemory",(void*)gv_free_memory,(void**)&gv_o_free_memory,NULL},
        {"vkMapMemory",(void*)gv_map_impl,(void**)&gv_o_map_impl,NULL},
        {"vkUnmapMemory",(void*)gv_unmap_impl,(void**)&gv_o_unmap_impl,NULL},
        {"vkBindBufferMemory",(void*)gv_bind,(void**)&gv_o_bind,NULL},
        {"vkUpdateDescriptorSets",(void*)gv_update,(void**)&gv_o_update,NULL},
        {"vkQueueSubmit",(void*)gv_submit,(void**)&gv_o_submit,NULL}};
    for(unsigned i=0;i<7;i++) {
        hooks[i].target=(void*)get(device,hooks[i].name);
        if(!hooks[i].target) return 0;
        uint32_t instruction;memcpy(&instruction,hooks[i].target,4);
        if(i==1 && (instruction&0xff00001fu)==0xb4000001u) hooks[i].target=(uint8_t*)hooks[i].target+4;
        /* The public map/unmap entry points are tiny tail-call wrappers. Resolve
         * only the exact captured register shuffles and validate the callee ABI
         * prologue. This avoids overwriting adjacent functions or relying on a
         * free executable page within a branch's 128 MiB range. */
        if(i==2) {
            uint32_t code[13];memcpy(code,hooks[i].target,sizeof(code));
            const uint32_t expected[]={0xb100047f,0xaa0103e0,0x540000a0,0xaa0203e1,0xaa0303e2,0xaa0503e3,
                0,0xf9407008,0xcb020103,0xaa0203e1,0xaa0303e2,0xaa0503e3,0};
            for(unsigned k=0;k<13;k++) if(k!=6 && k!=12 && code[k]!=expected[k]) return 0;
            if((code[6]&0xfc000000u)!=0x14000000u || (code[12]&0xfc000000u)!=0x14000000u) return 0;
            intptr_t a=(intptr_t)hooks[i].target+24+(int64_t)((int32_t)(code[6]<<6)>>6)*4;
            intptr_t b=(intptr_t)hooks[i].target+48+(int64_t)((int32_t)(code[12]<<6)>>6)*4;
            const uint32_t prologue[]={0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd};
            if(a!=b || memcmp((void*)a,prologue,sizeof(prologue))) return 0;
            hooks[i].target=(void*)a;
        }
        if(i==3) {
            uint32_t code[2];memcpy(code,hooks[i].target,sizeof(code));
            if(code[0]!=0xaa0103e0u || (code[1]&0xfc000000u)!=0x14000000u) return 0;
            intptr_t address=(intptr_t)hooks[i].target+4+(int64_t)((int32_t)(code[1]<<6)>>6)*4;
            const uint32_t prologue[]={0xf81e0ff3,0xa9017bfd,0x910043fd,0xb940f808};
            if(memcmp((void*)address,prologue,sizeof(prologue))) return 0;
            hooks[i].target=(void*)address;
        }
        uint32_t relocated[16];
        if(!cr_trampoline(relocated,(const uint32_t*)hooks[i].target,(uintptr_t)hooks[i].target)) {
            char b[100];snprintf(b,sizeof(b),"gpu view: hook rejected %s",hooks[i].name);_diag(b);return 0;
        }
    }
    for(unsigned i=0;i<7;i++) if(!_ih_patch(hooks[i].target,hooks[i].hook,hooks[i].original,tp+2304+i*64)) {
        char b[100];snprintf(b,sizeof(b),"gpu view: hook failed %s",hooks[i].name);_diag(b);return 0;
    }
    _diag("gpu view: uniform/submit hooks ready");return 1;
}
#endif
