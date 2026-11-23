#!/usr/bin/env python3
"""Run the production presentation path against a deterministic Vulkan recorder."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
header=(root/'vulkan_present.h').read_text()
types=header[header.index('typedef struct {'):header.index('static void vp_event')]
function=header[header.index('static VkResult vp_present('):header.index('static int vp_install(')]
source=r'''
#include <vulkan/vulkan.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#define VP_IMAGES 8
#define VP_SWAPS 4
#define syscall(...) 1L
#define __NR_gettid 0
static pthread_mutex_t vp_pixels_mutex=PTHREAD_MUTEX_INITIALIZER;
static uint8_t bytes[64], mapped[64], *vp_pixels=bytes;
static uint32_t vp_width=4,vp_height=4;
static uint64_t vp_generation=2;
static _Atomic unsigned vp_presented,vp_skipped;
static _Atomic int vp_active;
static PFN_vkQueuePresentKHR vp_o_present;
static VkPhysicalDevice vp_physical;
static struct {VkQueue queue;uint32_t family;} vp_queues[16];
static uint32_t vp_queue_count=1;
static VkResult fence_status=VK_SUCCESS,submit_status=VK_SUCCESS;
static int submits,presents,uploads,barriers,draws,initialized,cleanups;
static const VkPresentInfoKHR *original;
static int expect_composite=1;
static void _diag(const char *msg) {}
static void vp_report(const char *where,VkResult result) {}
'''+types+r'''
static int vp_initialize(VpSwap *s,uint32_t family,uint32_t w,uint32_t h) {initialized++;return 0;}
static void vp_cleanup(VpSwap *s) {cleanups++;s->ready=0;}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice p,uint32_t *count,VkQueueFamilyProperties *values) {
    *count=1;if(values) values[0].queueFlags=VK_QUEUE_GRAPHICS_BIT;
}
VKAPI_ATTR VkResult VKAPI_CALL vkGetFenceStatus(VkDevice d,VkFence f) {return fence_status;}
VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer c,VkCommandBufferResetFlags f) {return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer c,const VkCommandBufferBeginInfo *b) {return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer c) {return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer c,VkPipelineStageFlags src,VkPipelineStageFlags dst,VkDependencyFlags f,uint32_t nm,const VkMemoryBarrier *m,uint32_t nb,const VkBufferMemoryBarrier *b,uint32_t ni,const VkImageMemoryBarrier *i) {
    assert(ni==1);barriers++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdCopyBufferToImage(VkCommandBuffer c,VkBuffer b,VkImage i,VkImageLayout l,uint32_t n,const VkBufferImageCopy *copy) {
    assert(copy->imageExtent.width==4 && copy->imageExtent.height==4);uploads++;
}
VKAPI_ATTR void VKAPI_CALL vkCmdBeginRenderPass(VkCommandBuffer c,const VkRenderPassBeginInfo *rp,VkSubpassContents contents) {}
VKAPI_ATTR void VKAPI_CALL vkCmdBindPipeline(VkCommandBuffer c,VkPipelineBindPoint p,VkPipeline pipeline) {}
VKAPI_ATTR void VKAPI_CALL vkCmdBindDescriptorSets(VkCommandBuffer c,VkPipelineBindPoint p,VkPipelineLayout l,uint32_t first,uint32_t n,const VkDescriptorSet *sets,uint32_t dn,const uint32_t *offsets) {}
VKAPI_ATTR void VKAPI_CALL vkCmdDraw(VkCommandBuffer c,uint32_t count,uint32_t inst,uint32_t first,uint32_t fi) {assert(count==3 && inst==1);draws++;}
VKAPI_ATTR void VKAPI_CALL vkCmdEndRenderPass(VkCommandBuffer c) {}
VKAPI_ATTR VkResult VKAPI_CALL vkResetFences(VkDevice d,uint32_t count,const VkFence *f) {return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue q,uint32_t count,const VkSubmitInfo *info,VkFence f) {
    assert(count==1 && info->waitSemaphoreCount==2);
    assert(info->pWaitSemaphores==original->pWaitSemaphores);
    assert(info->pWaitDstStageMask[0]==VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    assert(info->signalSemaphoreCount==1 && info->pSignalSemaphores[0]==(VkSemaphore)35);
    submits++;return submit_status;
}
static VkResult present(VkQueue q,const VkPresentInfoKHR *info) {
    if(expect_composite) {
        assert(info!=original && info->waitSemaphoreCount==1 && info->pWaitSemaphores[0]==(VkSemaphore)35);
        assert(info->pSwapchains==original->pSwapchains && info->pImageIndices==original->pImageIndices);
        assert(info->pResults==original->pResults && info->pNext==original->pNext);
    } else assert(info==original);
    presents++;return VK_SUBOPTIMAL_KHR;
}
'''+function+r'''
int main(void) {
    VpSwap *s=&vp_swaps[0];s->handle=(VkSwapchainKHR)1;s->ready=1;s->count=1;s->tw=s->th=4;
    s->extent=(VkExtent2D){1280,720};
    VpFrame *f=&s->frames[0];f->done=(VkSemaphore)35;f->mapped=mapped;
    vp_queues[0].queue=(VkQueue)2;vp_queues[0].family=0;
    VkSemaphore semaphores[]={(VkSemaphore)11,(VkSemaphore)12};uint32_t index=0;
    VkPresentInfoKHR info={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=2,.pWaitSemaphores=semaphores,
        .swapchainCount=1,.pSwapchains=&s->handle,.pImageIndices=&index};
    original=&info;vp_o_present=present;
    memset(bytes,123,sizeof(bytes));
    assert(vp_present((VkQueue)2,&info)==VK_SUBOPTIMAL_KHR);
    assert(submits==1 && presents==1 && uploads==1 && barriers==2 && draws==1);
    assert(!memcmp(mapped,bytes,64) && f->generation==2 && f->initialized && atomic_load(&vp_active));
    /* Same pixels reuse the texture; each acquired image still gets a draw. */
    vp_present((VkQueue)2,&info);assert(submits==2 && uploads==1 && barriers==2);
    /* An in-flight image is never overwritten or blocked on the CPU. */
    expect_composite=0;fence_status=VK_NOT_READY;
    vp_present((VkQueue)2,&info);assert(submits==2 && presents==3);
    fence_status=VK_SUCCESS;
    index=1;vp_present((VkQueue)2,&info);assert(submits==2);index=0;
    submit_status=VK_ERROR_DEVICE_LOST;
    vp_present((VkQueue)2,&info);assert(submits==3 && s->failed && !atomic_load(&vp_active));
    vp_present((VkQueue)2,&info);assert(submits==3);
    assert(initialized==0 && cleanups==0);
    puts("Vulkan present: image fencing, upload reuse, semaphore chaining, result propagation and failure passthrough passed");
}
'''
ndk=Path(os.environ.get('NDK_HOME','/opt/homebrew/share/android-ndk'))
includes=next((ndk/'toolchains/llvm/prebuilt').glob('*/sysroot/usr/include'))
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c';binary=Path(d)/'test';c.write_text(source)
    subprocess.run(['cc','-std=gnu11','-idirafter',str(includes),str(c),'-pthread','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
