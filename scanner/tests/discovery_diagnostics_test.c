#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/syscall.h>
static long fixture_syscall(long number, ...);
#define syscall fixture_syscall
#define main scanner_program_main
#define __ANDROID_API__ 31
#include "../memscan.c"
#undef main
#undef syscall
#include <assert.h>

#define BASE UINT64_C(0x7000000000)
#define WORLD (BASE+0x10000)
#define LEVEL (BASE+0x20000)
#define CLUSTER (BASE+0x30000)
#define ARRAY (BASE+0x40000)
#define ACTOR (BASE+0x50000)
#define ROOT (BASE+0x60000)
#define GI (BASE+0x70000)
#define LPS (BASE+0x71000)
#define LP (BASE+0x72000)
#define PC (BASE+0x73000)
#define CLASS (BASE+0x74000)
static unsigned char memory[0x100000];
static uint64_t trace_hash, trace_calls;
static uint64_t fault_address;
static int fault_after, fault_hits, fault_partial;
static void put64(uint64_t addr, uint64_t value) { memcpy(memory+addr-BASE,&value,8); }
static void put32(uint64_t addr, int32_t value) { memcpy(memory+addr-BASE,&value,4); }
static void putfloat(uint64_t addr, float value) { memcpy(memory+addr-BASE,&value,4); }
static long fixture_syscall(long number, ...) {
    assert(number == __NR_process_vm_readv);
    va_list args; va_start(args, number);
    (void)va_arg(args,int);
    struct iovec *local=va_arg(args,struct iovec *);
    assert(va_arg(args,int)==1);
    struct iovec *remote=va_arg(args,struct iovec *);
    assert(va_arg(args,int)==1); (void)va_arg(args,int); va_end(args);
    uint64_t address=(uintptr_t)remote->iov_base; size_t size=remote->iov_len;
    trace_calls++; trace_hash=(trace_hash*UINT64_C(1099511628211)) ^ address ^ (size*17);
    if (address < BASE || address > BASE+sizeof(memory) || size > BASE+sizeof(memory)-address) { errno=EFAULT;return -1; }
    if (address==fault_address && ++fault_hits > fault_after) {
        if (!fault_partial) { errno=EIO;return -1; }
        size=size>8?8:0;
    }
    memcpy(local->iov_base,memory+address-BASE,size);
    return (long)size;
}
static void reset_trace(void) { trace_hash=0; trace_calls=0; fault_hits=0; errno=0; }
static void reset_fixture(void) {
    memset(memory,0,sizeof(memory)); memset(regions,0,sizeof(regions));
    memset(&discovery_diag,0,sizeof(discovery_diag));
    fault_address=0;fault_after=0;fault_partial=0;
    running=1;session_file=NULL;use_vm_readv=1;game_pid=42;
    ue4_base=BASE+0x1000;ue4_end=BASE+0x4000;
    put64(WORLD+0xf8,LEVEL);put64(LEVEL+0xa8,WORLD);
    put64(LEVEL+0xc8,CLUSTER);put64(CLUSTER+0x28,ARRAY);
    put32(CLUSTER+0x30,20);put32(CLUSTER+0x34,20);
    for(int i=0;i<20;i++) put64(ARRAY+i*8,ACTOR);
    put64(ACTOR+0x8,CLASS);put64(ACTOR+0x180,ROOT);
    put32(CLASS+0x24,2445);put64(CLASS+0x40,BASE+0x76000);put32(BASE+0x76000+0x24,2224);putfloat(ROOT+0x21c,1);
    putfloat(ROOT+0x220,100);putfloat(ROOT+0x224,200);putfloat(ROOT+0x228,300);
    put64(WORLD+0x190,GI);put64(GI+0x38,LPS);put64(LPS,LP);put64(LP+0x30,PC);
    put64(PC+OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN,ACTOR);
    num_regions=0;num_candidates=0;reset_trace();
}
static GWorldCandidate validate(int expected) {
    GWorldCandidate result={0}; diag_begin();
    int score=try_validate_uworld(WORLD,&result);
    assert(score==expected); discovery_diag.active=0; return result;
}
int main(void) {
    read_buf=malloc(READ_BUF_SIZE); assert(read_buf);
    reset_fixture(); assert(validate(230).plausible_players==20);
    reset_fixture(); put64(WORLD+0xf8,0); validate(0);
    assert(discovery_diag.rejected[DC_LEVEL_POINTER]==1);
    reset_fixture();fault_address=WORLD+0xf8;validate(0);
    assert(discovery_diag.reads[DS_LEVEL].failed==1);
    reset_fixture();put64(LEVEL+0xc8,0);put64(LEVEL+0x98,ARRAY);put32(LEVEL+0xa0,20);put32(LEVEL+0xa4,20);
    assert(validate(230).actor_source==0x98);
    reset_fixture();put64(LEVEL+0xc8,0);put64(LEVEL+0x1f0,ARRAY);put32(LEVEL+0x1f8,20);put32(LEVEL+0x1fc,20);
    validate(0); /* Model/component arrays must not become actor arrays. */
    reset_fixture();putfloat(ROOT+0x220,NAN);assert(validate(170).plausible_players==0);
    reset_fixture();put64(WORLD+0x190,0);assert(validate(110).plausible_players==0);
    reset_fixture();put64(ACTOR+0x180,0);assert(validate(170).plausible_players==0);
    reset_fixture();putfloat(ROOT+0x168,NAN);assert(validate(230).plausible_players==20);
    reset_fixture();putfloat(ROOT+0x21c,0);assert(validate(170).plausible_players==0);
    /* Native anchors require both the known class index and superclass index. */
    char name[96];uint64_t parent=BASE+0x76000;
    reset_fixture();put32(CLASS+0x24,2445);put64(CLASS+0x40,parent);put32(parent+0x24,2224);
    assert(resolve_class_name(CLASS,name,sizeof(name)) && !strcmp(name,"GPCharacterBase"));
    assert(is_character_class(CLASS,CLASS));
    put32(parent+0x24,1);assert(!resolve_class_name(CLASS,name,sizeof(name)));
    put32(CLASS+0x24,7826);put32(parent+0x24,7825);
    loot_class_cache_count=0;assert(loot_kind_from_class(CLASS)==4);
    /* Actual encoded None header/string observed on this build. */
    uint64_t pool=BASE+0x80000,block=BASE+0x90000;
    memset(fname_block_cache,0,sizeof(fname_block_cache));fname_pool_addr=pool;
    put64(pool+0x38,block);unsigned char entry[]={0x2a,0x01,0xb1,0x90,0x91,0x9a};
    memcpy(memory+block-BASE,entry,sizeof(entry));
    assert(resolve_fname(0,name,sizeof(name))==4 && !strcmp(name,"None"));
    fault_address=pool+0x38;assert(resolve_fname(0,name,sizeof(name))==4);
    char two[]={0x37,0x16,0};decode_name_bytes(two,2);assert(!strcmp(two,"Hi"));
    char world[]={0xa8,0x90,0x8d,0x93,0x9b,0};decode_name_bytes(world,5);assert(!strcmp(world,"World"));
    char persistent[]={0x2f,0x1a,0x0d,0x0c,0x16,0x0c,0x0b,0x1a,0x11,0x0b,0};
    decode_name_bytes(persistent,10);assert(!strcmp(persistent,"Persistent"));
    /* An invalid static world is one read, with no heap compatibility search. */
    reset_fixture();compatibility_scan=0;reset_trace();scan_for_gworld();assert(num_candidates==0 && trace_calls==1);
    free(read_buf);puts("SDK discovery, name decoding, class anchors and bounded-scan checks passed");return 0;
}
