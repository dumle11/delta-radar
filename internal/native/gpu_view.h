/* Capture vertex-shader interfaces for binding the scene's exact view buffer.
 * Observation only: no original shader or descriptor data is changed. */
#ifndef INTERNAL_GPU_VIEW_H
#define INTERNAL_GPU_VIEW_H
#include <vulkan/vulkan.h>
#include "gpu_view_profile.h"
#define GV_MODULES 512
static struct {VkShaderModule module;uint32_t *words;size_t count;GvProfile profile;} gv_modules[GV_MODULES];
static pthread_mutex_t gv_mutex=PTHREAD_MUTEX_INITIALIZER;
static PFN_vkCreateShaderModule gv_o_module;
static VkResult gv_create_module(VkDevice device,const VkShaderModuleCreateInfo *info,const VkAllocationCallbacks *alloc,VkShaderModule *module) {
    VkResult result=gv_o_module(device,info,alloc,module);
    if(result!=VK_SUCCESS || !info->pCode || info->codeSize<20 || info->codeSize>262144) return result;
    size_t count=info->codeSize/4;int vertex=0;
    for(size_t i=5;i<count;) {
        uint32_t n=info->pCode[i]>>16,op=info->pCode[i]&65535;
        if(!n || i+n>count) return result;
        if(op==15 && n>=4 && info->pCode[i+1]==0) vertex=1;
        i+=n;
    }
    if(!vertex) return result;
    GvProfile profile=gv_profile(info->pCode,count);
    if(!profile.valid) return result; /* Unsupported shaders cannot exhaust the view cache. */
    pthread_mutex_lock(&gv_mutex);
    int slot=-1;
    for(unsigned i=0;i<GV_MODULES;i++) {
        if(gv_modules[i].module==*module) {slot=(int)i;break;}
        if(slot<0 && !gv_modules[i].module) slot=(int)i;
    }
    if(slot>=0) {
        uint32_t *copy=malloc(info->codeSize);
        if(copy) {memcpy(copy,info->pCode,info->codeSize);free(gv_modules[slot].words);
            gv_modules[slot].words=copy;gv_modules[slot].module=*module;gv_modules[slot].count=count;gv_modules[slot].profile=profile;}
    }
    pthread_mutex_unlock(&gv_mutex);
    return result;
}
typedef struct {
    float matrix[16],translation[3];
    VkViewport viewport;
    uint32_t width,height;
    int64_t recorded_at;
    unsigned serial;
    int valid,screen_rotation;
} GvFrame;
#define GV_PIPELINES 1024
static struct {VkPipeline pipeline;GvProfile profile;} gv_pipelines[GV_PIPELINES];
static void gv_pipeline(VkPipeline pipeline,VkShaderModule module) {
    pthread_mutex_lock(&gv_mutex);
    GvProfile profile={0};
    for(unsigned i=0;i<GV_MODULES;i++) if(gv_modules[i].module==module) {profile=gv_modules[i].profile;break;}
    if(profile.valid) for(unsigned i=0;i<GV_PIPELINES;i++) if(!gv_pipelines[i].pipeline || gv_pipelines[i].pipeline==pipeline) {
        gv_pipelines[i].pipeline=pipeline;gv_pipelines[i].profile=profile;break;
    }
    pthread_mutex_unlock(&gv_mutex);
}
static void gv_log(void);
static void gv_cmd_reset(VkCommandBuffer);
static void gv_cmd_free(VkCommandBuffer);
static void gv_begin_pass(VkCommandBuffer,const VkRenderPassBeginInfo*);
static void gv_draw_view(VkCommandBuffer,VkPipeline);
static void gv_selected(VkShaderModule module) {
    static VkShaderModule recorded[3];static unsigned count;
    pthread_mutex_lock(&gv_mutex);
    if(count>=3) {pthread_mutex_unlock(&gv_mutex);return;}
    for(unsigned i=0;i<count;i++) if(recorded[i]==module) {pthread_mutex_unlock(&gv_mutex);return;}
    for(unsigned i=0;i<GV_MODULES;i++) if(gv_modules[i].module==module) {
        char path[100];snprintf(path,sizeof(path),"/data/data/com.proxima.dfm/cache/view%u.spv",count);
        FILE *file=fopen(path,"wb");
        if(file) {fwrite(gv_modules[i].words,4,gv_modules[i].count,file);fclose(file);
            char b[100];snprintf(b,sizeof(b),"gpu view: captured vertex=%u words=%zu",count,gv_modules[i].count);_diag(b);
            recorded[count++]=module;}
        break;
    }
    pthread_mutex_unlock(&gv_mutex);
}
#endif
