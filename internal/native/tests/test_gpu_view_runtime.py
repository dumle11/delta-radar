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
submit=header[header.index('static int gv_signal_slot('):header.index('static void gv_log(')]
installer=header[header.index('static int gv_install('):header.rindex('#endif')]
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
#include "chams_reloc.h"
#define CR_CB_CAP 128
static pthread_mutex_t gv_mutex=PTHREAD_MUTEX_INITIALIZER;
static unsigned char bytes[512];
static int64_t ef_now(void){return 1000;}
static int ev_read(uint64_t address,void*out,size_t size){memcpy(out,(void*)(uintptr_t)address,size);return 1;}
'''+frame+'\n'+functions+submit+r'''
static void _diag(const char *s){}
static unsigned patched;
static int _ih_patch(void *t,void *h,void **o,uint8_t *slot){patched++;*o=t;return 1;}
'''+installer+r'''
static VkDeviceMemory fixture_memory=(VkDeviceMemory)0x100000;
static VkResult allocate(VkDevice d,const VkMemoryAllocateInfo *i,const VkAllocationCallbacks *a,VkDeviceMemory *m){*m=fixture_memory;return VK_SUCCESS;}
static void freemem(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks *a){}
static VkResult map(VkDeviceMemory m,VkDeviceSize o,VkDeviceSize n,void **p){*p=bytes+o;return VK_SUCCESS;}
static VkResult map_public_fixture(VkDevice d,VkDeviceMemory m,VkDeviceSize o,VkDeviceSize n,VkMemoryMapFlags f,void **p){
 assert(d==(VkDevice)0xabc && f==0);*p=bytes+o;return VK_SUCCESS;
}
static uint32_t hook_code[7][64];
static const char *hook_names[]={"vkAllocateMemory","vkFreeMemory","vkMapMemory","vkUnmapMemory","vkBindBufferMemory","vkUpdateDescriptorSets","vkQueueSubmit"};
static PFN_vkVoidFunction fixture_get(VkDevice d,const char *name){for(unsigned i=0;i<7;i++)if(!strcmp(name,hook_names[i]))return (PFN_vkVoidFunction)hook_code[i];return NULL;}
static void install_fixture(void){
 uint8_t tramp[4096];memset(hook_code,0,sizeof(hook_code));
 const uint32_t plain[]={0xa9bd7bfd,0xa90157f6,0x910003fd,0xa9024ff4};
 for(unsigned i=0;i<7;i++)memcpy(hook_code[i],plain,16);
 hook_code[3][0]=0xd65f03c0;patched=0;
 assert(gv_install(fixture_get,NULL,tramp) && patched==6);
 // One unknown map instruction must reject the pair before any patch.
 hook_code[2][1]^=1;patched=0;assert(!gv_install(fixture_get,NULL,tramp) && !patched);
 const uint32_t map_old[]={0xb100047f,0xaa0103e0,0x540000a0,0xaa0203e1,0xaa0303e2,0xaa0503e3,0,0xf9407008,0xcb020103,0xaa0203e1,0xaa0303e2,0xaa0503e3,0};
 memcpy(hook_code[2],map_old,sizeof(map_old));
 const uint32_t map_body[]={0xf81d0ff5,0xa9014ff4,0xa9027bfd,0x910083fd};
 memcpy(&hook_code[2][32],map_body,16);
 hook_code[2][6]=0x14000000u|(32-6);hook_code[2][12]=0x14000000u|(32-12);
 hook_code[3][0]=0xaa0103e0;hook_code[3][1]=0x14000000u|(32-1);
 const uint32_t unmap_body[]={0xf81e0ff3,0xa9017bfd,0x910043fd,0xb940f808};
 memcpy(&hook_code[3][32],unmap_body,16);patched=0;
 assert(gv_install(fixture_get,NULL,tramp) && patched==7);
 hook_code[3][32]^=1;patched=0;assert(!gv_install(fixture_get,NULL,tramp) && !patched);
}
static VkResult unmap(VkDeviceMemory m){return VK_SUCCESS;}
static VkResult bind(VkDevice d,VkBuffer b,VkDeviceMemory m,VkDeviceSize o){return VK_SUCCESS;}
static void update(VkDevice d,uint32_t n,const VkWriteDescriptorSet*w,uint32_t nc,const VkCopyDescriptorSet*c){}
static VkResult submit_status=VK_SUCCESS;
static VkResult queue_submit(VkQueue q,uint32_t n,const VkSubmitInfo*s,VkFence f){return submit_status;}
int main(void){
 gv_o_allocate=allocate;gv_o_free_memory=freemem;gv_o_map_impl=map;gv_o_unmap_impl=unmap;gv_o_bind=bind;gv_o_update=update;gv_o_submit=queue_submit;
 VkMemoryAllocateInfo a={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.allocationSize=512};VkDeviceMemory memory;
 assert(gv_allocate(NULL,&a,NULL,&memory)==VK_SUCCESS);void *mapped;
 gv_o_map_public=map_public_fixture;
 assert(gv_map_public((VkDevice)0xabc,memory,16,VK_WHOLE_SIZE,0,&mapped)==VK_SUCCESS);
 assert(gv_memory[gv_memory_slot(memory,0)].mapped_size==a.allocationSize-16);
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
 VkSemaphore collision=(VkSemaphore)((uintptr_t)sem[1]+16);
 while(gv_hash((uint64_t)collision,512)!=gv_hash((uint64_t)sem[1],512)) collision=(VkSemaphore)((uintptr_t)collision+16);
 VkQueue other_queue=(VkQueue)0x910000;VkSubmitInfo other=submits[0];other.pSignalSemaphores=&collision;
 assert(gv_submit(other_queue,1,&other,0)==VK_SUCCESS);
 assert(gv_present_view(queue,&present,&view) && view.serial==17);
 VkPresentInfoKHR other_present=present;other_present.pWaitSemaphores=&collision;
 assert(gv_present_view(other_queue,&other_present,&view)&&view.serial==17);
 assert(!gv_present_view(other_queue,&other_present,&view)); // Binary wait consumed its metadata.
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
 subprocess.run(['cc','-std=gnu11','-I',str(root),'-idirafter',str(includes),str(c),'-pthread','-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
