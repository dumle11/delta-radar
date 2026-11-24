/* Composite the offscreen UI into the game's own swapchain. All Vulkan work runs
 * in the game's vkQueuePresentKHR call, with per-image fences/semaphores. */
#ifndef INTERNAL_VULKAN_PRESENT_H
#define INTERNAL_VULKAN_PRESENT_H
#include <vulkan/vulkan.h>
#include <android/bitmap.h>
#include <android/input.h>
#include "composite_spv.h"
#include "esp_gpu.h"
#define VP_IMAGES 8
#define VP_SWAPS 4
#define VP_MAX_PIXELS (4096u*4096u)
static pthread_mutex_t vp_pixels_mutex=PTHREAD_MUTEX_INITIALIZER;
static uint8_t *vp_pixels;
static uint32_t vp_width,vp_height;
static _Atomic int vp_rotation;
static uint64_t vp_generation;
static _Atomic unsigned vp_presented,vp_skipped;
static _Atomic int vp_active;
static PFN_vkCreateDevice vp_o_create_device;
static PFN_vkGetDeviceQueue vp_o_get_queue;
static PFN_vkCreateSwapchainKHR vp_o_create_swap;
static PFN_vkDestroySwapchainKHR vp_o_destroy_swap;
static PFN_vkQueuePresentKHR vp_o_present;
static VkPhysicalDevice vp_physical;
static VkDevice vp_device;
static struct { VkQueue queue; uint32_t family; } vp_queues[16];
static uint32_t vp_queue_count;
static JavaVM *vp_vm;
static jclass vp_ui_class;
static jmethodID vp_touch_method;
static float vp_hits[12];
static uint32_t vp_hit_count;
static int64_t vp_hit_at;
static int vp_touch_owned;
static int32_t (*vp_o_get_event)(AInputQueue*,AInputEvent**);

static void vp_regions(JNIEnv *env,jclass cls,jfloatArray regions) {
    jsize count=(*env)->GetArrayLength(env,regions);
    if(count>12 || count%4) return;
    pthread_mutex_lock(&vp_pixels_mutex);
    (*env)->GetFloatArrayRegion(env,regions,0,count,vp_hits);
    vp_hit_count=(uint32_t)count/4;vp_hit_at=ef_now();
    pthread_mutex_unlock(&vp_pixels_mutex);
}
static int vp_route(AInputEvent *event) {
    if(!vp_vm || !vp_ui_class || !vp_touch_method) return 0;
    JNIEnv *env=NULL;int attached=0;
    if((*vp_vm)->GetEnv(vp_vm,(void**)&env,JNI_VERSION_1_6)!=JNI_OK) {
        if((*vp_vm)->AttachCurrentThread(vp_vm,&env,NULL)!=JNI_OK) return 0;
        attached=1;
    }
    int action=AMotionEvent_getAction(event)&AMOTION_EVENT_ACTION_MASK;
    if(action==AMOTION_EVENT_ACTION_POINTER_DOWN || action==AMOTION_EVENT_ACTION_POINTER_UP) action=AMOTION_EVENT_ACTION_MOVE;
    jboolean accepted=(*env)->CallStaticBooleanMethod(env,vp_ui_class,vp_touch_method,
        action,AMotionEvent_getX(event,0),AMotionEvent_getY(event,0),
        (jlong)(AMotionEvent_getDownTime(event)/1000000),(jlong)(AMotionEvent_getEventTime(event)/1000000));
    if((*env)->ExceptionCheck(env)) {(*env)->ExceptionClear(env);accepted=0;}
    if(attached) (*vp_vm)->DetachCurrentThread(vp_vm);
    return accepted!=0;
}
static int32_t vp_get_event(AInputQueue *queue,AInputEvent **event) {
    for(;;) {
        int32_t result=vp_o_get_event(queue,event);
        if(result<0 || !event || !*event) return result;
        AInputEvent *input=*event;
        if(AInputEvent_getType(input)!=AINPUT_EVENT_TYPE_MOTION ||
           (AInputEvent_getSource(input)&AINPUT_SOURCE_TOUCHSCREEN)!=AINPUT_SOURCE_TOUCHSCREEN) return result;
        int action=AMotionEvent_getAction(input)&AMOTION_EVENT_ACTION_MASK;
        if(action==AMOTION_EVENT_ACTION_DOWN) {
            vp_touch_owned=0;
            if(atomic_load(&vp_active)) {
                float x=AMotionEvent_getX(input,0),y=AMotionEvent_getY(input,0);
                pthread_mutex_lock(&vp_pixels_mutex);
                if(ef_now()-vp_hit_at<=250) for(unsigned i=0;i<vp_hit_count;i++) {
                    const float *r=&vp_hits[i*4];
                    if(x>=r[0] && x<r[2] && y>=r[1] && y<r[3]) {vp_touch_owned=1;break;}
                }
                pthread_mutex_unlock(&vp_pixels_mutex);
            }
        }
        if(!vp_touch_owned) return result;
        int accepted=vp_route(input);
        if(!accepted && action==AMOTION_EVENT_ACTION_DOWN) {vp_touch_owned=0;return result;}
        AInputQueue_finishEvent(queue,input,1);
        if(action==AMOTION_EVENT_ACTION_UP || action==AMOTION_EVENT_ACTION_CANCEL) vp_touch_owned=0;
    }
}

typedef struct {
    VkImage image,texture;
    VkImageView view,texture_view;
    VkDeviceMemory texture_memory,buffer_memory;
    VkBuffer buffer;
    void *mapped;
    VkDescriptorSet descriptor;
    VkFramebuffer framebuffer;
    VkCommandBuffer command;
    VkFence fence;
    VkSemaphore done;
    VkBuffer esp_vb;
    VkDeviceMemory esp_vb_memory;
    void *esp_vb_mapped;
    uint64_t generation;
    int initialized;
} VpFrame;
typedef struct {
    VkSwapchainKHR handle;
    VkDevice device;
    VkFormat format;
    VkSurfaceTransformFlagBitsKHR transform;
    VkExtent2D extent;
    uint32_t count,tw,th,family;
    VkRenderPass pass;
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkPipelineLayout esp_layout;
    VkPipeline esp_pipeline;
    VkDescriptorSetLayout descriptor_layout;
    VkDescriptorPool descriptors;
    VkSampler sampler;
    VkCommandPool pool;
    VpFrame frames[VP_IMAGES];
    int ready,failed;
} VpSwap;
static VpSwap vp_swaps[VP_SWAPS];

static void vp_event(JNIEnv *env,jclass cls,jstring message) {
    const char *text=(*env)->GetStringUTFChars(env,message,NULL);
    if(text) { char b[220];snprintf(b,sizeof(b),"present UI: %.200s",text);_diag(b);(*env)->ReleaseStringUTFChars(env,message,text); }
}
static void vp_bitmap(JNIEnv *env,jclass cls,jobject bitmap,jint rotation) {
    atomic_store(&vp_rotation,rotation&3);
    if(!vp_ui_class) {
        (*env)->GetJavaVM(env,&vp_vm);
        vp_ui_class=(*env)->NewGlobalRef(env,cls);
        vp_touch_method=(*env)->GetStaticMethodID(env,cls,"routeTouch","(IFFJJ)Z");
        if((*env)->ExceptionCheck(env)) {(*env)->ExceptionClear(env);vp_touch_method=NULL;}
    }
    AndroidBitmapInfo info; void *pixels=NULL;
    if(AndroidBitmap_getInfo(env,bitmap,&info)!=ANDROID_BITMAP_RESULT_SUCCESS ||
       info.format!=ANDROID_BITMAP_FORMAT_RGBA_8888 || !info.width || !info.height ||
       (uint64_t)info.width*info.height>VP_MAX_PIXELS) return;
    if(AndroidBitmap_lockPixels(env,bitmap,&pixels)!=ANDROID_BITMAP_RESULT_SUCCESS) return;
    pthread_mutex_lock(&vp_pixels_mutex);
    if(info.width!=vp_width || info.height!=vp_height) {
        uint8_t *next=realloc(vp_pixels,(size_t)info.width*info.height*4);
        if(!next) { pthread_mutex_unlock(&vp_pixels_mutex); AndroidBitmap_unlockPixels(env,bitmap); return; }
        vp_pixels=next; vp_width=info.width; vp_height=info.height;
    }
    for(uint32_t y=0;y<info.height;y++)
        memcpy(vp_pixels+(size_t)y*info.width*4,(uint8_t*)pixels+(size_t)y*info.stride,(size_t)info.width*4);
    vp_generation++;
    if(vp_generation==1) _diag("present: first pixels captured");
    pthread_mutex_unlock(&vp_pixels_mutex);
    AndroidBitmap_unlockPixels(env,bitmap);
}
static jboolean vp_is_active(JNIEnv *env,jclass cls) { return atomic_load(&vp_active)!=0; }
static void vp_report(const char *where,VkResult result) {
    char b[100]; snprintf(b,sizeof(b),"present: %s rc=%d",where,result); _diag(b);
}
static uint32_t vp_memory(uint32_t bits,VkMemoryPropertyFlags flags) {
    VkPhysicalDeviceMemoryProperties properties;
    vkGetPhysicalDeviceMemoryProperties(vp_physical,&properties);
    for(uint32_t i=0;i<properties.memoryTypeCount;i++)
        if((bits&(1u<<i)) && (properties.memoryTypes[i].propertyFlags&flags)==flags) return i;
    return UINT32_MAX;
}
static void vp_cleanup(VpSwap *s) {
    if(!s->device) return;
    /* Only on swapchain retirement/resize, never on the steady-state path. */
    vkDeviceWaitIdle(s->device);
    for(uint32_t i=0;i<s->count;i++) {
        VpFrame *f=&s->frames[i];
        if(f->mapped) vkUnmapMemory(s->device,f->buffer_memory);
        if(f->esp_vb_mapped) vkUnmapMemory(s->device,f->esp_vb_memory);
        if(f->framebuffer) vkDestroyFramebuffer(s->device,f->framebuffer,NULL);
        if(f->texture_view) vkDestroyImageView(s->device,f->texture_view,NULL);
        if(f->view) vkDestroyImageView(s->device,f->view,NULL);
        if(f->texture) vkDestroyImage(s->device,f->texture,NULL);
        if(f->buffer) vkDestroyBuffer(s->device,f->buffer,NULL);
        if(f->esp_vb) vkDestroyBuffer(s->device,f->esp_vb,NULL);
        if(f->texture_memory) vkFreeMemory(s->device,f->texture_memory,NULL);
        if(f->buffer_memory) vkFreeMemory(s->device,f->buffer_memory,NULL);
        if(f->esp_vb_memory) vkFreeMemory(s->device,f->esp_vb_memory,NULL);
        if(f->done) vkDestroySemaphore(s->device,f->done,NULL);
        if(f->fence) vkDestroyFence(s->device,f->fence,NULL);
        VkImage image=f->image; memset(f,0,sizeof(*f)); f->image=image;
    }
    if(s->pool) vkDestroyCommandPool(s->device,s->pool,NULL);
    if(s->pipeline) vkDestroyPipeline(s->device,s->pipeline,NULL);
    if(s->esp_pipeline) vkDestroyPipeline(s->device,s->esp_pipeline,NULL);
    if(s->layout) vkDestroyPipelineLayout(s->device,s->layout,NULL);
    if(s->esp_layout) vkDestroyPipelineLayout(s->device,s->esp_layout,NULL);
    if(s->descriptors) vkDestroyDescriptorPool(s->device,s->descriptors,NULL);
    if(s->descriptor_layout) vkDestroyDescriptorSetLayout(s->device,s->descriptor_layout,NULL);
    if(s->sampler) vkDestroySampler(s->device,s->sampler,NULL);
    if(s->pass) vkDestroyRenderPass(s->device,s->pass,NULL);
    s->pool=0;s->pipeline=0;s->esp_pipeline=0;s->layout=0;s->esp_layout=0;s->descriptors=0;s->descriptor_layout=0;s->sampler=0;s->pass=0;s->ready=0;
}
#define VP_TRY(call) do { VkResult result=(call); if(result!=VK_SUCCESS) { vp_report(#call,result); goto fail; } } while(0)
static int vp_initialize(VpSwap *s,uint32_t family,uint32_t width,uint32_t height) {
    s->tw=width;s->th=height;s->family=family;
    VkAttachmentDescription attachment={.format=s->format,.samples=VK_SAMPLE_COUNT_1_BIT,
        .loadOp=VK_ATTACHMENT_LOAD_OP_LOAD,.storeOp=VK_ATTACHMENT_STORE_OP_STORE,
        .stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE,.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE,
        .initialLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,.finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass={.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS,.colorAttachmentCount=1,.pColorAttachments=&color};
    VkSubpassDependency dependencies[2]={
        {.srcSubpass=VK_SUBPASS_EXTERNAL,.dstSubpass=0,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
         .dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT},
        {.srcSubpass=0,.dstSubpass=VK_SUBPASS_EXTERNAL,.srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
         .dstStageMask=VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT}};
    VkRenderPassCreateInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,.attachmentCount=1,.pAttachments=&attachment,
        .subpassCount=1,.pSubpasses=&subpass,.dependencyCount=2,.pDependencies=dependencies};
    VP_TRY(vkCreateRenderPass(s->device,&rp,NULL,&s->pass));
    VkDescriptorSetLayoutBinding binding={.binding=0,.descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount=1,.stageFlags=VK_SHADER_STAGE_FRAGMENT_BIT};
    VkDescriptorSetLayoutCreateInfo dl={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,.bindingCount=1,.pBindings=&binding};
    VP_TRY(vkCreateDescriptorSetLayout(s->device,&dl,NULL,&s->descriptor_layout));
    VkPipelineLayoutCreateInfo pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.setLayoutCount=1,.pSetLayouts=&s->descriptor_layout};
    VP_TRY(vkCreatePipelineLayout(s->device,&pl,NULL,&s->layout));
    VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,s->count};
    VkDescriptorPoolCreateInfo dp={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,.maxSets=s->count,.poolSizeCount=1,.pPoolSizes=&pool_size};
    VP_TRY(vkCreateDescriptorPool(s->device,&dp,NULL,&s->descriptors));
    VkSamplerCreateInfo sampler={.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,.magFilter=VK_FILTER_LINEAR,.minFilter=VK_FILTER_LINEAR,
        .mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST,.addressModeU=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,.maxLod=0};
    VP_TRY(vkCreateSampler(s->device,&sampler,NULL,&s->sampler));
    VkShaderModule modules[2]={0};
    VkShaderModuleCreateInfo sm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(vp_vert_spv),.pCode=vp_vert_spv};
    VkResult result=vkCreateShaderModule(s->device,&sm,NULL,&modules[0]);
    if(result!=VK_SUCCESS) goto fail;
    sm.codeSize=sizeof(vp_frag_spv);sm.pCode=vp_frag_spv;
    result=vkCreateShaderModule(s->device,&sm,NULL,&modules[1]);
    if(result!=VK_SUCCESS) { vkDestroyShaderModule(s->device,modules[0],NULL); goto fail; }
    VkBool32 srgb=(s->format==VK_FORMAT_R8G8B8A8_SRGB || s->format==VK_FORMAT_B8G8R8A8_SRGB);
    int rotation=s->transform==VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR?1:
        s->transform==VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR?2:
        s->transform==VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR?3:0;
    if(!rotation && ((s->extent.width>s->extent.height)!=(width>height))) rotation=atomic_load(&vp_rotation);
    uint32_t constants[2]={srgb,(uint32_t)rotation};
    VkSpecializationMapEntry entries[2]={{0,0,4},{1,4,4}};
    VkSpecializationInfo spec={2,entries,sizeof(constants),constants};
    VkPipelineShaderStageCreateInfo stages[2]={
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=modules[0],.pName="main"},
        {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=modules[1],.pName="main",.pSpecializationInfo=&spec}};
    VkPipelineVertexInputStateCreateInfo vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkViewport viewport={0,0,(float)s->extent.width,(float)s->extent.height,0,1};
    VkRect2D scissor={{0,0},s->extent};
    VkPipelineViewportStateCreateInfo vs={.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,.viewportCount=1,.pViewports=&viewport,.scissorCount=1,.pScissors=&scissor};
    VkPipelineRasterizationStateCreateInfo rs={.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,.polygonMode=VK_POLYGON_MODE_FILL,.cullMode=VK_CULL_MODE_NONE,.lineWidth=1};
    VkPipelineMultisampleStateCreateInfo ms={.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState blend={.blendEnable=VK_TRUE,.srcColorBlendFactor=VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,.colorBlendOp=VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .alphaBlendOp=VK_BLEND_OP_ADD,.colorWriteMask=15};
    VkPipelineColorBlendStateCreateInfo bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&blend};
    VkGraphicsPipelineCreateInfo gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=stages,.pVertexInputState=&vi,
        .pInputAssemblyState=&ia,.pViewportState=&vs,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&bs,.layout=s->layout,.renderPass=s->pass};
    result=vkCreateGraphicsPipelines(s->device,0,1,&gp,NULL,&s->pipeline);
    vkDestroyShaderModule(s->device,modules[0],NULL);vkDestroyShaderModule(s->device,modules[1],NULL);
    if(result!=VK_SUCCESS) { vp_report("pipeline",result); goto fail; }
    /* ESP line pipeline: vertex-colored triangles (line quads), push constants for screen size */
    {
        VkPushConstantRange pc_range={.stageFlags=VK_SHADER_STAGE_VERTEX_BIT,.offset=0,.size=8};
        VkPipelineLayoutCreateInfo esp_pl={.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,.pushConstantRangeCount=1,.pPushConstantRanges=&pc_range};
        VP_TRY(vkCreatePipelineLayout(s->device,&esp_pl,NULL,&s->esp_layout));
        VkShaderModule esp_modules[2]={0};
        VkShaderModuleCreateInfo esm={.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,.codeSize=sizeof(eg_vert_spv),.pCode=eg_vert_spv};
        result=vkCreateShaderModule(s->device,&esm,NULL,&esp_modules[0]);
        if(result!=VK_SUCCESS) goto fail;
        esm.codeSize=sizeof(eg_frag_spv);esm.pCode=eg_frag_spv;
        result=vkCreateShaderModule(s->device,&esm,NULL,&esp_modules[1]);
        if(result!=VK_SUCCESS) {vkDestroyShaderModule(s->device,esp_modules[0],NULL);goto fail;}
        VkPipelineShaderStageCreateInfo esp_stages[2]={
            {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_VERTEX_BIT,.module=esp_modules[0],.pName="main"},
            {.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,.stage=VK_SHADER_STAGE_FRAGMENT_BIT,.module=esp_modules[1],.pName="main"}};
        VkVertexInputBindingDescription esp_binding={.binding=0,.stride=12,.inputRate=VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription esp_attrs[2]={
            {.location=0,.binding=0,.format=VK_FORMAT_R32G32_SFLOAT,.offset=0},
            {.location=1,.binding=0,.format=VK_FORMAT_R8G8B8A8_UNORM,.offset=8}};
        VkPipelineVertexInputStateCreateInfo esp_vi={.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount=1,.pVertexBindingDescriptions=&esp_binding,
            .vertexAttributeDescriptionCount=2,.pVertexAttributeDescriptions=esp_attrs};
        VkPipelineInputAssemblyStateCreateInfo esp_ia={.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
        VkPipelineColorBlendAttachmentState esp_blend={.blendEnable=VK_TRUE,
            .srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA,.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,.colorBlendOp=VK_BLEND_OP_ADD,
            .srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE,.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,.alphaBlendOp=VK_BLEND_OP_ADD,.colorWriteMask=15};
        VkPipelineColorBlendStateCreateInfo esp_bs={.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,.attachmentCount=1,.pAttachments=&esp_blend};
        VkGraphicsPipelineCreateInfo esp_gp={.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,.stageCount=2,.pStages=esp_stages,.pVertexInputState=&esp_vi,
            .pInputAssemblyState=&esp_ia,.pViewportState=&vs,.pRasterizationState=&rs,.pMultisampleState=&ms,.pColorBlendState=&esp_bs,.layout=s->esp_layout,.renderPass=s->pass};
        result=vkCreateGraphicsPipelines(s->device,0,1,&esp_gp,NULL,&s->esp_pipeline);
        vkDestroyShaderModule(s->device,esp_modules[0],NULL);vkDestroyShaderModule(s->device,esp_modules[1],NULL);
        if(result!=VK_SUCCESS) {vp_report("esp pipeline",result);goto fail;}
    }
    VkCommandPoolCreateInfo cp={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=family};
    VP_TRY(vkCreateCommandPool(s->device,&cp,NULL,&s->pool));
    for(uint32_t i=0;i<s->count;i++) {
        VpFrame *f=&s->frames[i];
        VkImageViewCreateInfo view={.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,.image=f->image,.viewType=VK_IMAGE_VIEW_TYPE_2D,
            .format=s->format,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        VP_TRY(vkCreateImageView(s->device,&view,NULL,&f->view));
        VkFramebufferCreateInfo fb={.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,.renderPass=s->pass,.attachmentCount=1,.pAttachments=&f->view,
            .width=s->extent.width,.height=s->extent.height,.layers=1};
        VP_TRY(vkCreateFramebuffer(s->device,&fb,NULL,&f->framebuffer));
        VkImageCreateInfo image={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,.imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,
            .extent={width,height,1},.mipLevels=1,.arrayLayers=1,.samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
            .usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
        VP_TRY(vkCreateImage(s->device,&image,NULL,&f->texture));
        VkMemoryRequirements req; vkGetImageMemoryRequirements(s->device,f->texture,&req);
        uint32_t mt=vp_memory(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if(mt==UINT32_MAX) goto fail;
        VkMemoryAllocateInfo ma={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=req.size,.memoryTypeIndex=mt};
        VP_TRY(vkAllocateMemory(s->device,&ma,NULL,&f->texture_memory));
        VP_TRY(vkBindImageMemory(s->device,f->texture,f->texture_memory,0));
        view.image=f->texture;view.format=VK_FORMAT_R8G8B8A8_UNORM;
        VP_TRY(vkCreateImageView(s->device,&view,NULL,&f->texture_view));
        VkBufferCreateInfo buffer={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=(VkDeviceSize)width*height*4,.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT};
        VP_TRY(vkCreateBuffer(s->device,&buffer,NULL,&f->buffer));
        vkGetBufferMemoryRequirements(s->device,f->buffer,&req);
        mt=vp_memory(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(mt==UINT32_MAX) goto fail;
        ma.allocationSize=req.size;ma.memoryTypeIndex=mt;
        VP_TRY(vkAllocateMemory(s->device,&ma,NULL,&f->buffer_memory));
        VP_TRY(vkBindBufferMemory(s->device,f->buffer,f->buffer_memory,0));
        VP_TRY(vkMapMemory(s->device,f->buffer_memory,0,VK_WHOLE_SIZE,0,&f->mapped));
        /* ESP vertex buffer (host-visible for per-frame writes) */
        VkBufferCreateInfo esp_buf={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.size=EG_VB_SIZE,.usage=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT};
        VP_TRY(vkCreateBuffer(s->device,&esp_buf,NULL,&f->esp_vb));
        vkGetBufferMemoryRequirements(s->device,f->esp_vb,&req);
        mt=vp_memory(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(mt==UINT32_MAX) goto fail;
        ma.allocationSize=req.size;ma.memoryTypeIndex=mt;
        VP_TRY(vkAllocateMemory(s->device,&ma,NULL,&f->esp_vb_memory));
        VP_TRY(vkBindBufferMemory(s->device,f->esp_vb,f->esp_vb_memory,0));
        VP_TRY(vkMapMemory(s->device,f->esp_vb_memory,0,VK_WHOLE_SIZE,0,&f->esp_vb_mapped));
        VkDescriptorSetAllocateInfo da={.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,.descriptorPool=s->descriptors,.descriptorSetCount=1,.pSetLayouts=&s->descriptor_layout};
        VP_TRY(vkAllocateDescriptorSets(s->device,&da,&f->descriptor));
        VkDescriptorImageInfo di={s->sampler,f->texture_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=f->descriptor,.dstBinding=0,.descriptorCount=1,
            .descriptorType=VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,.pImageInfo=&di};
        vkUpdateDescriptorSets(s->device,1,&write,0,NULL);
        VkCommandBufferAllocateInfo ca={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=s->pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
        VP_TRY(vkAllocateCommandBuffers(s->device,&ca,&f->command));
        VkFenceCreateInfo fc={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,.flags=VK_FENCE_CREATE_SIGNALED_BIT};
        VP_TRY(vkCreateFence(s->device,&fc,NULL,&f->fence));
        VkSemaphoreCreateInfo se={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VP_TRY(vkCreateSemaphore(s->device,&se,NULL,&f->done));
    }
    s->ready=1;
    char b[120];snprintf(b,sizeof(b),"present: ready images=%u extent=%ux%u ui=%ux%u family=%u transform=%u rotation=%d",s->count,s->extent.width,s->extent.height,width,height,family,s->transform,rotation);_diag(b);
    return 1;
fail:
    vp_cleanup(s);s->failed=1;return 0;
}
#undef VP_TRY
static VkResult vp_create_device(VkPhysicalDevice physical,const VkDeviceCreateInfo *info,const VkAllocationCallbacks *alloc,VkDevice *device) {
    VkResult r=vp_o_create_device(physical,info,alloc,device);
    if(r==VK_SUCCESS && !vp_device) { vp_device=*device;vp_physical=physical; _diag("present: device captured"); }
    return r;
}
static void vp_get_queue(VkDevice device,uint32_t family,uint32_t index,VkQueue *queue) {
    vp_o_get_queue(device,family,index,queue);
    if(device==vp_device && vp_queue_count<16) {
        for(uint32_t i=0;i<vp_queue_count;i++) if(vp_queues[i].queue==*queue) return;
        vp_queues[vp_queue_count].queue=*queue;vp_queues[vp_queue_count++].family=family;
    }
}
static VkResult vp_create_swap(VkDevice device,const VkSwapchainCreateInfoKHR *info,const VkAllocationCallbacks *alloc,VkSwapchainKHR *swap) {
    VkResult r=vp_o_create_swap(device,info,alloc,swap);
    if(r!=VK_SUCCESS || device!=vp_device || !(info->imageUsage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) || info->imageArrayLayers!=1) return r;
    for(unsigned i=0;i<VP_SWAPS;i++) if(!vp_swaps[i].handle) {
        VpSwap *s=&vp_swaps[i];s->handle=*swap;s->device=device;s->format=info->imageFormat;s->extent=info->imageExtent;s->transform=info->preTransform;
        uint32_t count=0;
        if(vkGetSwapchainImagesKHR(device,*swap,&count,NULL)!=VK_SUCCESS || count>VP_IMAGES || !count) { s->failed=1;break; }
        VkImage images[VP_IMAGES];
        if(vkGetSwapchainImagesKHR(device,*swap,&count,images)!=VK_SUCCESS) { s->failed=1;break; }
        s->count=count;for(uint32_t j=0;j<count;j++) s->frames[j].image=images[j];
        _diag("present: swapchain captured");break;
    }
    return r;
}
static void vp_destroy_swap(VkDevice device,VkSwapchainKHR swap,const VkAllocationCallbacks *alloc) {
    for(unsigned i=0;i<VP_SWAPS;i++) if(vp_swaps[i].handle==swap) {
        vp_cleanup(&vp_swaps[i]);memset(&vp_swaps[i],0,sizeof(vp_swaps[i]));atomic_store(&vp_active,0);break;
    }
    vp_o_destroy_swap(device,swap,alloc);
}
static VkResult vp_present(VkQueue queue,const VkPresentInfoKHR *info) {
#ifdef INTERNAL_ENGINE_CAMERA_H
    ec_note_present();
#endif
    VpSwap *s=NULL;
    if(info->swapchainCount==1 && info->waitSemaphoreCount<=32)
        for(unsigned i=0;i<VP_SWAPS;i++) if(vp_swaps[i].handle==info->pSwapchains[0]) {s=&vp_swaps[i];break;}
    uint32_t family=UINT32_MAX;
    for(uint32_t i=0;i<vp_queue_count;i++) if(vp_queues[i].queue==queue) family=vp_queues[i].family;
    if(!s || s->failed || family==UINT32_MAX || info->pImageIndices[0]>=s->count) goto skip;
    uint32_t nf=0;vkGetPhysicalDeviceQueueFamilyProperties(vp_physical,&nf,NULL);
    if(family>=nf || nf>32) goto skip;
    VkQueueFamilyProperties properties[32];vkGetPhysicalDeviceQueueFamilyProperties(vp_physical,&nf,properties);
    if(!(properties[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)) goto skip;
    pthread_mutex_lock(&vp_pixels_mutex);
    uint32_t w=vp_width,h=vp_height;
    pthread_mutex_unlock(&vp_pixels_mutex);
    if(!w || !h) goto skip;
    if(s->ready && (s->tw!=w || s->th!=h || s->family!=family)) vp_cleanup(s);
    if(!s->ready && !vp_initialize(s,family,w,h)) goto skip;
    VpFrame *f=&s->frames[info->pImageIndices[0]];
    if(vkGetFenceStatus(s->device,f->fence)!=VK_SUCCESS) goto skip;
    pthread_mutex_lock(&vp_pixels_mutex);
    if(vp_width!=s->tw || vp_height!=s->th) {pthread_mutex_unlock(&vp_pixels_mutex);goto skip;}
    int upload=(f->generation!=vp_generation);
    uint64_t generation=vp_generation;
    if(upload) memcpy(f->mapped,vp_pixels,(size_t)s->tw*s->th*4);
    pthread_mutex_unlock(&vp_pixels_mutex);
    VkResult result=vkResetCommandBuffer(f->command,0);
    if(result!=VK_SUCCESS) goto skip;
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if(vkBeginCommandBuffer(f->command,&begin)!=VK_SUCCESS) goto skip;
    if(upload) {
        VkImageMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask=f->initialized?VK_ACCESS_SHADER_READ_BIT:0,.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,
            .oldLayout=f->initialized?VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
            .image=f->texture,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
        vkCmdPipelineBarrier(f->command,f->initialized?VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
        VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},.imageExtent={s->tw,s->th,1}};
        vkCmdCopyBufferToImage(f->command,f->buffer,f->texture,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        vkCmdPipelineBarrier(f->command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
    }
    VkRenderPassBeginInfo rp={.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,.renderPass=s->pass,.framebuffer=f->framebuffer,.renderArea={{0,0},s->extent}};
    vkCmdBeginRenderPass(f->command,&rp,VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(f->command,VK_PIPELINE_BIND_POINT_GRAPHICS,s->pipeline);
    vkCmdBindDescriptorSets(f->command,VK_PIPELINE_BIND_POINT_GRAPHICS,s->layout,0,1,&f->descriptor,0,NULL);
    vkCmdDraw(f->command,3,1,0,0);
    /* Draw ESP geometry with fresh camera data, accounting for surface rotation */
    if(s->esp_pipeline && f->esp_vb_mapped) {
        int rot=s->transform==VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR?1:
            s->transform==VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR?2:
            s->transform==VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR?3:0;
        if(!rot && ((s->extent.width>s->extent.height)!=(s->tw>s->th))) rot=atomic_load(&vp_rotation);
        float lw,lh;
        if(rot==1||rot==3) { lw=(float)s->extent.height; lh=(float)s->extent.width; }
        else { lw=(float)s->extent.width; lh=(float)s->extent.height; }
        GvFrame render_view={0};
        gv_present_view(queue,info,&render_view);
        if((render_view.width>render_view.height)!=(lw>lh)) render_view.screen_rotation=rot;
        int esp_verts=eg_draw((EgVert*)f->esp_vb_mapped,lw,lh,&render_view);
        if(esp_verts>0) {
            if(rot) {
                EgVert *vb=(EgVert*)f->esp_vb_mapped;
                for(int vi=0;vi<esp_verts;vi++) {
                    float x=vb[vi].x, y=vb[vi].y;
                    if(rot==1)      { vb[vi].x=lh-y; vb[vi].y=x; }
                    else if(rot==2) { vb[vi].x=lw-x; vb[vi].y=lh-y; }
                    else            { vb[vi].x=y;     vb[vi].y=lw-x; }
                }
            }
            float pc[2]={(float)s->extent.width,(float)s->extent.height};
            vkCmdBindPipeline(f->command,VK_PIPELINE_BIND_POINT_GRAPHICS,s->esp_pipeline);
            vkCmdPushConstants(f->command,s->esp_layout,VK_SHADER_STAGE_VERTEX_BIT,0,8,pc);
            VkDeviceSize zero=0;
            vkCmdBindVertexBuffers(f->command,0,1,&f->esp_vb,&zero);
            vkCmdDraw(f->command,(uint32_t)esp_verts,1,0,0);
        }
    }
    vkCmdEndRenderPass(f->command);
    if(vkEndCommandBuffer(f->command)!=VK_SUCCESS) goto skip;
    VkPipelineStageFlags waits[32];for(uint32_t i=0;i<info->waitSemaphoreCount;i++) waits[i]=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.waitSemaphoreCount=info->waitSemaphoreCount,.pWaitSemaphores=info->pWaitSemaphores,
        .pWaitDstStageMask=waits,.commandBufferCount=1,.pCommandBuffers=&f->command,.signalSemaphoreCount=1,.pSignalSemaphores=&f->done};
    if(vkResetFences(s->device,1,&f->fence)!=VK_SUCCESS) goto skip;
    gv_internal_submit++;
    result=vkQueueSubmit(queue,1,&submit,f->fence);
    gv_internal_submit--;
    if(result!=VK_SUCCESS) {vp_report("submit",result);s->failed=1;atomic_store(&vp_active,0);goto skip;}
    f->initialized=1;f->generation=generation;
    VkPresentInfoKHR present=*info;present.waitSemaphoreCount=1;present.pWaitSemaphores=&f->done;
    atomic_store(&vp_active,1);unsigned n=atomic_fetch_add(&vp_presented,1)+1;
    if(n==1 || n%300==0) {char b[100];snprintf(b,sizeof(b),"present: composited=%u skipped=%u tid=%ld",n,atomic_load(&vp_skipped),syscall(__NR_gettid));_diag(b);}
    return vp_o_present(queue,&present);
skip:
    { unsigned n=atomic_fetch_add(&vp_skipped,1)+1;
      if(n==1 || n%300==0) {char b[140];snprintf(b,sizeof(b),"present: skip=%u swap=%d family=%u pixels=%ux%u gen=%llu",n,s!=NULL,family,vp_width,vp_height,(unsigned long long)vp_generation);_diag(b);} }
    return vp_o_present(queue,info);
}
static int vp_install(PFN_vkGetDeviceProcAddr get,VkDevice device,void *create_device,uint8_t *tp) {
    struct {const char *name;void *hook;void **original;void *target;} hooks[]={
        {"vkCreateDevice",(void*)vp_create_device,(void**)&vp_o_create_device,create_device},
        {"vkGetDeviceQueue",(void*)vp_get_queue,(void**)&vp_o_get_queue,NULL},
        {"vkCreateSwapchainKHR",(void*)vp_create_swap,(void**)&vp_o_create_swap,NULL},
        {"vkDestroySwapchainKHR",(void*)vp_destroy_swap,(void**)&vp_o_destroy_swap,NULL},
        {"vkQueuePresentKHR",(void*)vp_present,(void**)&vp_o_present,NULL},
        {"AInputQueue_getEvent",(void*)vp_get_event,(void**)&vp_o_get_event,dlsym(RTLD_DEFAULT,"AInputQueue_getEvent")}};
    for(unsigned i=0;i<6;i++) {
        if(!hooks[i].target) hooks[i].target=(void*)get(device,hooks[i].name);
        uint32_t out[16];
        if(!hooks[i].target || !cr_trampoline(out,(const uint32_t*)hooks[i].target,(uintptr_t)hooks[i].target)) {
            char b[100];snprintf(b,sizeof(b),"present: hook preflight rejected %s target=%p",hooks[i].name,hooks[i].target);_diag(b);return 0;
        }
    }
    for(unsigned i=0;i<6;i++) if(!_ih_patch(hooks[i].target,hooks[i].hook,hooks[i].original,tp+1536+i*64)) return 0;
    _diag("present: hooks ready");return 1;
}
#endif
