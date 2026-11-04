/* Sparse synthetic address space: no process or game memory is accessed. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
static int game_pid=42,use_vm_readv=1,num_regions=1;
static uint64_t ue4_base=UINT64_C(0x1000000000);
static struct {uint64_t start;char name[64];} regions[1];
static struct {uint64_t base;size_t size;unsigned char *bytes;} blocks[20];
static int block_count;
static uint64_t mutate;
static void *address(uint64_t at,size_t n) {
    for(int i=0;i<block_count;i++) if(at>=blocks[i].base && n<=blocks[i].size && at-blocks[i].base<=blocks[i].size-n)
        return blocks[i].bytes+(at-blocks[i].base);
    return NULL;
}
static void allocate(uint64_t at,size_t n) {
    assert(block_count<20);blocks[block_count].base=at;blocks[block_count].size=n;
    blocks[block_count++].bytes=calloc(1,n);assert(blocks[block_count-1].bytes);
}
static ssize_t read_mem(uint64_t at,void *out,size_t n) {
    void *p=address(at,n);if(!p)return -1;memcpy(out,p,n);
    if(mutate==at && n==2400) {((unsigned char *)p)[0]^=1;mutate=0;}
    return (ssize_t)n;
}
static int enc_handler_unmanaged(const unsigned char *p) {return p[0]==255&&p[1]==255&&p[2]==0;}
#include "../position-decoder.h"
static void write32(uint64_t at,uint32_t n){void *p=address(at,4);assert(p);memcpy(p,&n,4);}
static void write64(uint64_t at,uint64_t n){void *p=address(at,8);assert(p);memcpy(p,&n,8);}
static const uint64_t trampoline=UINT64_C(0x2000000100),code=UINT64_C(0x2100000000),primary=UINT64_C(0x2200000000);
static const uint64_t object=UINT64_C(0x2300000000),context=UINT64_C(0x2400000000),libc=UINT64_C(0x2500000000);
static const uint64_t thread=UINT64_C(0x2600000000),tls=UINT64_C(0x2700000000),table=UINT64_C(0x2800000000);
static const uint64_t sorted=UINT64_C(0x2900000000),actor=UINT64_C(0x3000000000),root=UINT64_C(0x3100000000),mesh=UINT64_C(0x3200000000);
static void setup(int modern) {
    for(int i=0;i<block_count;i++)free(blocks[i].bytes);
    block_count=0;memset(&position_decoder,0,sizeof(position_decoder));
    allocate(ue4_base+0x107bbb8c,32);allocate(trampoline-8,176);allocate(code,0x5ed00);
    allocate(primary,128);allocate(object,160);allocate(context,0x2d000);allocate(libc,0x100000);
    allocate(thread,0x400);allocate(tls,64);allocate(table,4800);allocate(sorted,8);
    allocate(actor,0x400);allocate(root,0x300);allocate(mesh,0x300);
    uint32_t a[]={0x91084000,0x58000050,0xd61f0200};memcpy(address(ue4_base+0x107bbb8c,12),a,12);
    write64(ue4_base+0x107bbb98,trampoline);write32(trampoline+0x8c,0x58fffb60);write64(trampoline+0xa0,code);
    uint32_t b[]={0x14000002,0xd65f03c0,0x14000001,0xa9ba7bfd};memcpy(address(code,16),b,16);
    uint32_t c[]={0xd10283ff,0xf90023fe,0xa9056ffc,0xa90667fa};memcpy(address(code+0x5e150,16),c,16);
    write64(trampoline-8,primary);write64(primary+0x10,primary+0x20);write64(primary+0x30,primary+0x40);
    write64(primary+0x40,object);write64(code+0x5ec28,primary);
    write64(object+8,position_encode64(context,object^UINT64_C(0xd6f0bce)));
    write32(object+0x90,0x80000001);write32(object+0x94,1);write32(context+0x28cf0,123);
    regions[0].start=libc;strcpy(regions[0].name,"/apex/com.android.runtime/lib64/bionic/libc.so");
    const unsigned char oldid[]={0xe5,0x5e,0x6e,0x4c,0x63,0x15,0x09,0x59,0x86,0x33,0x76,0x97,0x98,0x68,0x30,0x23};
    const unsigned char newid[]={0xbb,0xbd,0xeb,0x7c,0x87,0xc7,0x4f,0x14,0x91,0xf9,0x2c,0x6e,0x60,0x50,0x95,0xb0};
    memcpy(address(libc+(modern?0x340:0x1b478),16),modern?newid:oldid,16);
    write64(object+0x58,libc+(modern?0xdf684:0xe7070));
    write64(libc+(modern?0xf25c8:0xf75f8),thread);write32(thread+16,123);
    write64(thread+(modern?0x2f8:0x2b0),tls);write64(libc+(modern?0xf4618:0xf8350)+16,3);
    write64(tls+16,3);write64(tls+24,UINT64_C(0x5003c5000));
    write64(context+0x9d0,UINT64_C(0x5003c6000));write32(context+0x28ce0,12);
    write32(context+0x20+45*4,2);write64(context+0x29548+45*8,position_encode64(table,57));
    write64(context+0x2a470+45*8,sorted);
    write64(context+0x2a1d0+0x28,position_encode64(1,0));
    write64(actor+0x180,root);write64(actor+0x3d0,mesh);
}
static void record(unsigned row,uint64_t component,const float xyz[3]) {
    PositionVectorState state;assert(position_vector_state(&state));
    uint64_t at=table+row*2400;write64(at+0x910,position_encode64(component+0x154,state.codec.salt));
    write64(at+0x918,component+0x148);write32(at+0x938,123);
    unsigned slot=position_vector_slot(&state.codec,123);uint32_t bits[3];memcpy(bits,xyz,12);
    uint64_t xy=position_encode64(bits[0]|((uint64_t)bits[1]<<32),state.codec.tls);
    uint64_t yz=position_encode64((xy>>32)|((uint64_t)bits[2]<<32),state.codec.tls);
    write32(at+8+slot*12,(uint32_t)xy);write32(at+12+slot*12,(uint32_t)yz);
    write32(at+16+slot*12,(uint32_t)(yz>>32)^(state.codec.mask&(uint32_t)state.codec.tls));
}
int main(void) {
    for(int modern=0;modern<=1;modern++) {
        setup(modern);assert(position_decoder_discover());
        assert(position_decoder.tls_pointer==thread+(modern?0x2f8:0x2b0));
        const float xyz[]={1000,2000,3000};record(0,root,xyz);record(1,mesh,xyz);
        int root_first=position_encode64(root+0x154,0)<position_encode64(mesh+0x154,0);
        write32(sorted,root_first?0:1);write32(sorted+4,root_first?1:0);
        float out[3];assert(position_decode_actor(actor,root,out));assert(!memcmp(out,xyz,12));
        write64(tls+16,5);assert(!position_decode_actor(actor,root,out));write64(tls+16,3);
        write64(table+0x918,mesh);assert(!position_decode_component(root,out));write64(table+0x918,root+0x148);
        mutate=table;assert(!position_decode_component(root,out));
        write32(context+0x2a1d0,1);assert(!position_decode_component(root,out));write32(context+0x2a1d0,0);
        write64(object+0x58,libc+1);position_decoder.refresh_ms=0;assert(!position_decoder_discover());
        setup(modern);((unsigned char *)address(libc+(modern?0x340:0x1b478),1))[0]^=1;
        assert(!position_decoder_discover());
    }
    for(int i=0;i<block_count;i++)free(blocks[i].bytes);
    puts("Android 10/12 decoder profiles, TLS generations, lookup, ownership and unstable-state rejection passed");
}
