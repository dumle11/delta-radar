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
#include "discovery_reference.c.inc"
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
    putfloat(ROOT+0x164,100);putfloat(ROOT+0x168,200);putfloat(ROOT+0x16c,300);
    put64(WORLD+0x190,GI);put64(GI+0x38,LPS);put64(LPS,LP);put64(LP+0x30,PC);
    put64(PC+OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN,ACTOR);
    num_regions=0;num_candidates=0;reset_trace();
}
static void compare_validator(const char *name,uint64_t world,int expected) {
    GWorldCandidate original={0},instrumented={0};
    reset_trace();discovery_diag.active=0;
    int score=reference_try_validate_uworld(world,&original);
    uint64_t calls=trace_calls,hash=trace_hash;
    diag_begin();diag_candidate_begin(world);reset_trace();
    int observed=try_validate_uworld(world,&instrumented);
    (void)D_CHECK(DC_SCORE_THRESHOLD,observed>=70);diag_candidate_finish(observed,instrumented.plausible_players);
    assert(score==observed && observed==expected);
    assert(memcmp(&original,&instrumented,sizeof(original))==0);
    assert(calls==trace_calls && hash==trace_hash); /* no added/reordered reads */
    printf("validator %s score=%d reads=%llu: identical\n",name,score,(unsigned long long)calls);
    discovery_diag.active=0;
}
static void mapping(int index,uint64_t start,uint64_t size,const char *perms,const char *label) {
    regions[index].start=start;regions[index].end=start+size;
    snprintf(regions[index].perms,8,"%s",perms);snprintf(regions[index].name,256,"%s",label);
    num_regions=index+1;
}
static void compare_scan(const char *name) {
    GWorldCandidate original[64];
    discovery_diag.active=0;reset_trace();reference_scan_for_gworld();
    int count=num_candidates;memcpy(original,candidates,sizeof(original));
    uint64_t calls=trace_calls,hash=trace_hash;
    num_candidates=0;memset(candidates,0,sizeof(candidates));reset_trace();
    diag_last_emit_ms=0;scan_for_gworld();
    assert(count==num_candidates && memcmp(original,candidates,(size_t)count*sizeof(*candidates))==0);
    assert(calls==trace_calls && hash==trace_hash);
    printf("scan %s candidates=%d reads=%llu: identical\n",name,count,(unsigned long long)calls);
}
static void report_to_stdout(void) { diag_last_emit_ms=0;assert(diag_report(stdout)==1); }
int main(void) {
    read_buf=malloc(READ_BUF_SIZE);assert(read_buf);
    reset_fixture();compare_validator("accepted",WORLD,230);
    assert(discovery_diag.score_ge70==1 && discovery_diag.plausible_players==20);
    reset_fixture();compare_validator("world_in_module",ue4_base,0);
    assert(discovery_diag.rejected[DC_WORLD_OUTSIDE_MODULE]==1 && trace_calls==0);
    reset_fixture();put64(WORLD+0xf8,0);compare_validator("invalid_level",WORLD,0);
    assert(discovery_diag.rejected[DC_LEVEL_POINTER]==1);
    reset_fixture();fault_address=WORLD+0xf8;compare_validator("level_read_error",WORLD,0);
    assert(discovery_diag.reads[DS_LEVEL].failed==1 && discovery_diag.errno_counts[EIO]==1);
    reset_fixture();put64(WORLD+0xf8,ue4_base);compare_validator("level_in_module",WORLD,0);
    assert(discovery_diag.rejected[DC_LEVEL_OUTSIDE_MODULE]==1);
    reset_fixture();put64(LEVEL+0xc8,0);compare_validator("no_list",WORLD,0);
    assert(discovery_diag.rejected[DC_BEST_LIST]==1 && discovery_diag.level_valid==1);
    reset_fixture();put32(CLUSTER+0x30,4);compare_validator("count_low",WORLD,0);
    assert(discovery_diag.path_rejected[0][DC_COUNT_MIN]==1 && discovery_diag.path_rejected[0][DC_COUNT_MAX]==0);
    reset_fixture();put32(CLUSTER+0x30,50001);compare_validator("count_high",WORLD,0);
    reset_fixture();put32(CLUSTER+0x34,19);compare_validator("capacity_low",WORLD,0);
    reset_fixture();put32(CLUSTER+0x34,100001);compare_validator("capacity_high",WORLD,0);
    reset_fixture();put64(CLUSTER+0x28,3);compare_validator("array_pointer",WORLD,0);
    reset_fixture();fault_address=ARRAY;compare_validator("array_read_error",WORLD,0);
    assert(discovery_diag.rejected[DC_ARRAY_READ]==1 && discovery_diag.reads[DS_ARRAY_CONTENTS].failed==1);
    reset_fixture();fault_address=ARRAY;fault_partial=1;compare_validator("array_partial",WORLD,0);
    assert(discovery_diag.reads[DS_ARRAY_CONTENTS].partial==1 && discovery_diag.errno_counts[EIO]==0);
    reset_fixture();for(int i=2;i<20;i++)put64(ARRAY+i*8,0);compare_validator("under_three",WORLD,0);
    assert(discovery_diag.rejected[DC_THREE_POINTERS]==1 && discovery_diag.max_raw_score==20);
    reset_fixture();for(int i=3;i<20;i++)put64(ARRAY+i*8,0);put64(WORLD+0x190,0);compare_validator("score_below_70",WORLD,25);
    assert(discovery_diag.max_score==25 && discovery_diag.rejected[DC_SCORE_THRESHOLD]==1);
    reset_fixture();put64(WORLD+0x190,0);compare_validator("no_local_class",WORLD,110);
    assert(discovery_diag.rejected[DC_LOCAL_CLASS]==20 && discovery_diag.score_ge70_with_players==0);
    reset_fixture();fault_address=ARRAY;fault_after=1;compare_validator("player_sample_read_error",WORLD,170);
    assert(discovery_diag.rejected[DC_SAMPLE_READ]==1);
    reset_fixture();put64(ACTOR+0x180,0);compare_validator("invalid_root",WORLD,170);
    reset_fixture();putfloat(ROOT+0x164,NAN);compare_validator("nan_x",WORLD,170);
    assert(discovery_diag.rejected[DC_X_FINITE]==20 && discovery_diag.rejected[DC_Y_FINITE]==0);
    reset_fixture();putfloat(ROOT+0x168,NAN);compare_validator("nan_y",WORLD,170);
    reset_fixture();putfloat(ROOT+0x16c,NAN);compare_validator("nan_z",WORLD,170);
    reset_fixture();putfloat(ROOT+0x164,INFINITY);compare_validator("infinity_x",WORLD,170);
    reset_fixture();putfloat(ROOT+0x168,10000001);compare_validator("large_y",WORLD,170);
    reset_fixture();putfloat(ROOT+0x16c,-10000001);compare_validator("large_z",WORLD,170);
    reset_fixture();putfloat(ROOT+0x164,0);putfloat(ROOT+0x168,0);putfloat(ROOT+0x16c,0);compare_validator("zero_position",WORLD,170);
    reset_fixture();put64(LEVEL+0xc8,0);put64(LEVEL+0xb0,ARRAY);put32(LEVEL+0xb8,20);put32(LEVEL+0xbc,20);compare_validator("direct_array",WORLD,230);

    reset_fixture();mapping(0,ue4_base,4096,"r--p","libUE4.so");mapping(1,ue4_base,4096,"rwxp","libUE4.so");
    mapping(2,ue4_base,4096,"rw-p","[anon:excluded]");mapping(3,BASE,4096,"rw-p","");
    mapping(4,ue4_base,2048,"rw-p","libUE4.so");mapping(5,ue4_base,4096,"rw-p","libUE4.so");
    compare_scan("all_map_gates_zero_pointers");
    assert(discovery_diag.map_count[DM_SELECTED]==1 && discovery_diag.map_count[DM_NOT_RW]==1 && discovery_diag.map_count[DM_EXECUTABLE]==1);
    assert(discovery_diag.map_count[DM_NAMED_OTHER]==1 && discovery_diag.map_count[DM_ANON_OUTSIDE]==1 && discovery_diag.map_count[DM_SIZE]==1);
    assert(discovery_diag.validation_calls==0 && discovery_diag.slots==512);report_to_stdout();
    reset_fixture();mapping(0,ue4_base,4096,"rw-p","libUE4.so");put64(ue4_base,WORLD);put64(WORLD+0xf8,0);
    compare_scan("examined_rejected");assert(discovery_diag.validation_calls==1 && discovery_diag.score_ge70==0);report_to_stdout();
    reset_fixture();mapping(0,ue4_base,4096,"rw-p","libUE4.so");fault_address=ue4_base;
    compare_scan("bulk_failed");assert(discovery_diag.slots==0 && discovery_diag.reads[DS_BULK].failed==1);report_to_stdout();
    reset_fixture();mapping(0,ue4_base,4096,"rw-p","libUE4.so");fault_address=ue4_base;fault_partial=1;
    compare_scan("bulk_partial");assert(discovery_diag.reads[DS_BULK].partial>0 && discovery_diag.slots>0);
    reset_fixture();mapping(0,ue4_base,4096,"rw-p","libUE4.so");
    for(int i=0;i<64;i++)put64(ue4_base+i*8,WORLD);
    mapping(1,ue4_base+4096,4096,"rw-p","libUE4.so");
    compare_scan("candidate_cap");assert(num_candidates==64 && discovery_diag.maps_visited==1 && discovery_diag.top_count==1);report_to_stdout();
    reset_fixture();for(int i=0;i<100;i++)mapping(i,ue4_base,4096,"rw-p","libUE4.so");
    compare_scan("bounded_map_details");assert(discovery_diag.mapping_count==32 && discovery_diag.map_count[DM_SELECTED]==100);report_to_stdout();
    assert(diag_report(stdout)==0 && diag_suppressed_passes==1);
    diag_begin();diag_candidate_begin(1);diag_candidate_finish(0,0);diag_candidate_begin(1);diag_candidate_finish(100,0);
    assert(discovery_diag.top_count==1 && discovery_diag.top[0].score==100);
    /* Diagnostic read accounting must preserve errno and ignore stale errno on a partial result. */
    diag_begin();diag_stage(DS_LEVEL);errno=EACCES;unsigned char buffer[8];assert(read_mem(WORLD,buffer,8)==8 && errno==EACCES);
    assert(discovery_diag.reads[DS_LEVEL].complete==1 && discovery_diag.errno_counts[EACCES]==0);
    discovery_diag.active=0;free(read_buf);puts("ALL DIFFERENTIAL AND DIAGNOSTIC CHECKS PASSED");return 0;
}
