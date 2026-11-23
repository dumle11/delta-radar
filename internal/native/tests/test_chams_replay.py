#!/usr/bin/env python3
"""Exercise actual replay journal with a mocked Vulkan command recorder."""
import os
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
ndk = Path(os.environ.get('NDK_HOME', '/opt/homebrew/share/android-ndk'))
includes = next((ndk / 'toolchains/llvm/prebuilt').glob('*/sysroot/usr/include'))
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
typedef void *VkCmdBuf_;
typedef void *VkPipe_;
static int draws, sequence[32], pushed, ended;
static VkPipe_ bound, host=(void*)99;
static void bind(VkCmdBuf_ cmd,uint32_t point,VkPipe_ p) { bound=p; }
static void draw(VkCmdBuf_ cmd,uint32_t ic,uint32_t inst,uint32_t fi,int32_t vo,uint32_t first) {
    assert(!ended); assert(pushed==42);
    sequence[draws++]=(int)(uintptr_t)bound;
}
static void (*_o_vk_bp)(VkCmdBuf_,uint32_t,VkPipe_)=bind;
static void (*_o_vk_di)(VkCmdBuf_,uint32_t,uint32_t,uint32_t,int32_t,uint32_t)=draw;
static VkPipe_ _vk_cur(VkCmdBuf_ cmd) { return host; }
static void _vk_set(VkCmdBuf_ cmd,VkPipe_ p) {}
static void _diag(const char *msg) {}
static int _ih_patch(void *p,void *hook,void **orig,uint8_t *slot) { return 0; }
#include "chams_reloc.h"
#include "chams_replay.h"
static VkResult begin(VkCommandBuffer cmd,const VkCommandBufferBeginInfo *info) { return VK_SUCCESS; }
static void beginpass(VkCommandBuffer cmd,const VkRenderPassBeginInfo *info,VkSubpassContents contents) { ended=0; }
static void endpass(VkCommandBuffer cmd) { ended=1; }
static void nextpass(VkCommandBuffer cmd,VkSubpassContents contents) {}
static void push(VkCommandBuffer cmd,VkPipelineLayout layout,VkShaderStageFlags stages,uint32_t offset,uint32_t size,const void *data) {
    assert(size==4 && offset==0); memcpy(&pushed,data,4);
}
static void freecmd(VkDevice dev,VkCommandPool pool,uint32_t count,const VkCommandBuffer *cmds) {}
int main(void) {
    VkCommandBuffer cmd=(VkCommandBuffer)1, other=(VkCommandBuffer)2;
    VkCommandBufferBeginInfo info={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cr_o_begin=begin; cr_o_begin_pass=beginpass; cr_o_end_pass=endpass;
    cr_o_next=nextpass; cr_o_free=freecmd; cr_o_vkCmdPushConstants=push; cr_ready=1;
    cr_begin(cmd,&info);
    int data=42;
    /* State set before render pass must be retained, and pointed-to bytes copied. */
    cr_vkCmdPushConstants(cmd,(VkPipelineLayout)1,1,0,4,&data); data=12;
    cr_pipeline((void*)cmd,host);
    cr_begin_pass(cmd,NULL,VK_SUBPASS_CONTENTS_INLINE);
    assert(cr_capture((void*)cmd,(void*)10,(void*)20,6,1,0,0,0));
    assert(cr_capture((void*)cmd,(void*)11,(void*)21,3,1,0,0,0));
    assert(draws==0); pushed=0;
    cr_end_pass(cmd);
    assert(draws==4 && sequence[0]==10 && sequence[1]==11 && sequence[2]==20 && sequence[3]==21);
    assert(bound==host && pushed==42 && ended);
    /* No old draws may leak into a later subpass; inherited state survives. */
    cr_begin_pass(cmd,NULL,VK_SUBPASS_CONTENTS_INLINE);
    assert(cr_capture((void*)cmd,(void*)12,(void*)22,6,1,0,0,0));
    cr_next(cmd,VK_SUBPASS_CONTENTS_INLINE);
    assert(draws==6 && sequence[4]==12 && sequence[5]==22);
    cr_end_pass(cmd); assert(draws==6);
    /* Independent buffers and beginning a new recording cannot reuse old draws. */
    cr_begin(other,&info); assert(cr_get(other,0)->count==0);
    cr_begin(cmd,&info); assert(cr_get(cmd,0)->count==0);
    cr_begin_pass(cmd,NULL,VK_SUBPASS_CONTENTS_INLINE);
    CrBuffer *b=cr_get(cmd,0); b->count=CR_EVENT_CAP;
    assert(cr_capture((void*)cmd,(void*)13,(void*)23,6,1,0,0,0));
    assert(b->bad); cr_end_pass(cmd); assert(draws==6);
    /* Secondary recording and passes using secondary contents suppress overlay. */
    info.flags=VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
    cr_begin(cmd,&info); cr_begin_pass(cmd,NULL,VK_SUBPASS_CONTENTS_INLINE);
    assert(cr_capture((void*)cmd,(void*)13,(void*)23,6,1,0,0,0));
    cr_end_pass(cmd); assert(draws==6);
    cr_free(NULL,0,1,&cmd); assert(cr_get(cmd,0)==NULL);
    puts("chams journal: ordered colors, state copies, pass isolation, reset, overflow and secondary checks passed");
}
'''
with tempfile.TemporaryDirectory() as directory:
    c = Path(directory) / 'test.c'
    binary = Path(directory) / 'test'
    c.write_text(source)
    subprocess.run(['cc', '-std=gnu11', '-Werror', '-I', str(root), '-idirafter', str(includes), str(c), '-o', str(binary), '-pthread'], check=True)
    subprocess.run([str(binary)], check=True)
