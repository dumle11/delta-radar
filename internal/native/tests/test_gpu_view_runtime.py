#!/usr/bin/env python3
"""Production mapped-uniform bounds and submit-to-present frame association."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
header=(root/'gpu_view_runtime.h').read_text()
frame=(root/'gpu_view.h').read_text();frame=frame[frame.index('typedef struct {'):frame.index('} GvFrame;')+len('} GvFrame;')]
functions=header[header.index('#define GV_MEMORY_CAP'):header.index('static void gv_draw_view(')]
submit=header[header.index('static VkResult gv_submit('):header.index('static void gv_log(')]
source=r'''
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdatomic.h>
#include <pthread.h>
#define CR_CB_CAP 128
static pthread_mutex_t gv_mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned char bytes[512];
static int64_t ef_now(void){return 1000;}
static int ev_read(uint64_t address,void*out,size_t size){memcpy(out,(void*)(uintptr_t)address,size);return 1;}
'''+frame+'\n'+functions+submit+r'''
static VkDeviceMemory fixture_memory=(VkDeviceMemory)0x100000;
static VkResult allocate(VkDevice d,const VkMemoryAllocateInfo *i,const VkAllocationCallbacks *a,VkDeviceMemory *m){*m=fixture_memory;return VK_SUCCESS;}
static void freemem(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks *a){}
static VkResult map(VkDeviceMemory m,VkDeviceSize o,VkDeviceSize n,void **p){*p=bytes+o;return VK_SUCCESS;}
static VkResult unmap(VkDeviceMemory m){return VK_SUCCESS;}
static VkResult bind(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize o){return VK_SUCCESS;}
static void update(VkDevice d,uint32_t n,const VkWriteDescriptorSet*w,uint32_t nc,const VkCopyDescriptorSet*c){}
static VkResult submit_status=VK_SUCCESS;
static VkResult queue_submit(VkQueue q,uint32_t n,const VkSubmitInfo*s,VkFence f){return submit_status;}
int main(void){
 gv_o_allocate=allocate;gv_o_free_memory=freemem;gv_o_map_impl=map;gv_o_unmap_impl=unmap;gv_o_bind=bind;gv_o_update=update;gv_o_submit=queue_submit;
 VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=512};VkDeviceMemory memory;
 assert(gv_allocate(NULL,&a,NULL,&memory)==VK_SUCCESS);void *mapped;
 assert(gv_map_impl(memory,16,VK_WHOLE_SIZE,&mapped)==VK_SUCCESS);
 VkBuffer buffer=(VkBuffer)0x200000;assert(gv_bind(NULL,buffer,memory,64)==VK_SUCCESS);
 VkDescriptorBufferInfo info={buffer,32,64};memset(bytes,123,sizeof(bytes));bytes[112]=42;
 unsigned char result[32];assert(gv_uniform_read(info,16,result,32) && result[0]==42);
 assert(!gv_uniform_read(info,16,result,65));assert(!gv_uniform_read(info,1000,result,1));
 info.offset=UINT64_MAX;assert(!gv_uniform_read(info,16,result,1));info.offset=32;
 gv_unmap_impl(memory);assert(!gv_uniform_read(info,16,result,1));gv_map_impl(memory,16,VK_WHOLE_SIZE,&mapped);
 VkWriteDescriptorSet write={.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,.dstSet=(VkDescriptorSet)0x300000,.dstBinding=0,
     .descriptorCount=1,.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,.pBufferInfo=&info};
 gv_update(NULL,1,&write,0,NULL);unsigned slot=gv_hash(0x300000,GV_SET_CAP);assert(gv_sets[slot].valid[0] && gv_sets[slot].buffers[0].offset==32);
 VkCopyDescriptorSet copy={.srcSet=write.dstSet,.srcBinding=0,.dstSet=(VkDescriptorSet)0x400000,.dstBinding=1,.descriptorCount=1};
 gv_update(NULL,0,NULL,1,&copy);slot=gv_hash(0x400000,GV_SET_CAP);assert(gv_sets[slot].valid[1] && gv_sets[slot].types[1]==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
 // Force the same starting hash for two live allocations, buffers and sets.
 VkDeviceMemory second_memory=(VkDeviceMemory)((uintptr_t)memory+16);
 while(gv_hash((uint64_t)second_memory,GV_MEMORY_CAP)!=gv_hash((uint64_t)memory,GV_MEMORY_CAP)) second_memory=(VkDeviceMemory)((uintptr_t)second_memory+16);
 fixture_memory=second_memory;assert(gv_allocate(NULL,&a,NULL,&second_memory)==VK_SUCCESS);
 assert(gv_map_impl(second_memory,16,VK_WHOLE_SIZE,&mapped)==VK_SUCCESS);
 VkBuffer second_buffer=(VkBuffer)((uintptr_t)buffer+16);
 while(gv_hash((uint64_t)second_buffer,GV_BUFFER_CAP)!=gv_hash((uint64_t)buffer,GV_BUFFER_CAP)) second_buffer=(VkBuffer)((uintptr_t)second_buffer+16);
 assert(gv_bind(NULL,second_buffer,second_memory,64)==VK_SUCCESS);
 VkDescriptorSet second_set=(VkDescriptorSet)((uintptr_t)write.dstSet+16);
 while(gv_hash((uint64_t)second_set,GV_SET_CAP)!=gv_hash((uint64_t)write.dstSet,GV_SET_CAP)) second_set=(VkDescriptorSet)((uintptr_t)second_set+16);
 VkWriteDescriptorSet second_write=write;second_write.dstSet=second_set;
 VkDescriptorBufferInfo second_info=info;second_info.buffer=second_buffer;second_write.pBufferInfo=&second_info;
 gv_update(NULL,1,&second_write,0,NULL);
 assert(gv_sets[gv_set_slot(write.dstSet,0)].buffers[0].buffer==buffer);
 assert(gv_sets[gv_set_slot(second_set,0)].buffers[0].buffer==second_buffer);
 assert(gv_uniform_read(info,16,result,32)&&gv_uniform_read(second_info,16,result,32));
 gv_free_memory(NULL,memory,NULL);assert(!gv_uniform_read(info,16,result,1));
 assert(gv_uniform_read(second_info,16,result,32)); // Lookup must survive a deleted collision-chain entry.
 fixture_memory=memory;assert(gv_allocate(NULL,&a,NULL,&memory)==VK_SUCCESS);
 assert(gv_memory_slot(second_memory,0)>=0 && gv_memory_slot(memory,0)>=0);
 gv_free_memory(NULL,second_memory,NULL);gv_free_memory(NULL,memory,NULL);
 VkCommandBuffer scene=(VkCommandBuffer)0x500000,post=(VkCommandBuffer)0x600000;gv_cmd_reset(scene);gv_cmd_reset(post);
 slot=gv_cmd_slot(scene,0);gv_commands[slot].view=(GvFrame){.valid=1,.recorded_at=1000,.serial=17};
 VkSemaphore sem[2]={(VkSemaphore)0x700000,(VkSemaphore)0x800000};
 VkSubmitInfo submits[2]={{.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&scene,.signalSemaphoreCount=1,.pSignalSemaphores=&sem[0]},
     {.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&post,.signalSemaphoreCount=1,.pSignalSemaphores=&sem[1]}};
 VkQueue queue=(VkQueue)0x900000;assert(gv_submit(queue,2,submits,0)==VK_SUCCESS);
 VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,.waitSemaphoreCount=1,.pWaitSemaphores=&sem[1]};GvFrame view;
 assert(gv_present_view(queue,&present,&view) && view.serial==17);
 // A subsequent frame with no scene draw cannot inherit the previous frame's camera.
 sem[1]=(VkSemaphore)0xa00000;assert(gv_submit(queue,1,&submits[1],0)==VK_SUCCESS);
 assert(!gv_present_view(queue,&present,&view));
 gv_cmd_reset(scene);gv_commands[slot].view=(GvFrame){.valid=1,.recorded_at=1000,.serial=18};
 submit_status=VK_ERROR_DEVICE_LOST;sem[0]=(VkSemaphore)0xb00000;
 assert(gv_submit(queue,1,submits,0)==VK_ERROR_DEVICE_LOST);present.pWaitSemaphores=&sem[0];assert(!gv_present_view(queue,&present,&view));
 gv_cmd_free(scene);assert(gv_cmd_slot(scene,0)<0);
 puts("GPU view: mapped bounds/unmap, descriptor copies, exact submit/present association and previous-frame rejection passed");
}
'''
ndk=Path(os.environ.get('NDK_HOME','/opt/homebrew/share/android-ndk'))
includes=next((ndk/'toolchains/llvm/prebuilt').glob('*/sysroot/usr/include'))
with tempfile.TemporaryDirectory() as d:
 c=Path(d)/'test.c';binary=Path(d)/'test';c.write_text(source)
 subprocess.run(['cc','-std=gnu11','-idirafter',str(includes),str(c),'-pthread','-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
