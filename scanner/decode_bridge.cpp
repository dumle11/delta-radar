/* Read-only integration with the GPL-2.0 decoder in vendor/dfm-decoder.
 * All writes and simulated system calls are confined to Unicorn memory. */
#include "vendor/dfm-decoder/sjz_dec_arm64.h"
#include "decoder_schedule.h"
#include "position_cache.h"
#include <unicorn/unicorn.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <dirent.h>
#include <sys/uio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <time.h>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <fcntl.h>
#include <vector>
#include <string>
#include <sstream>
extern int emulated_target_pid,emulated_target_tid;
static int target_pid=0;
static uintptr_t module_base=0;
static bool ready=false;
static long long initialize_retry_at=0;
// The SDK's second-stage decoder globals reside in a write-only .bss VMA.
// Force reads are limited to that one identified VMA and 256 KiB per call.
static uintptr_t protected_begin=0, protected_end=0;
static size_t protected_budget=0;
static size_t protected_reads=0;
static constexpr uint64_t GUEST_FRAME=0x7200000200ULL;
static constexpr uint64_t GUEST_RESULT=0x7200000300ULL;
static bool read_only(uintptr_t address,void *buffer,size_t size) {
 address&=0x00ffffffffffffffULL;
 iovec local={buffer,size},remote={(void*)address,size};
 if(syscall(__NR_process_vm_readv,target_pid,&local,1,&remote,1,0)==(ssize_t)size)return true;
 if(!protected_begin||address<protected_begin||address>=protected_end||
    size>protected_end-address||size>4096||size>protected_budget)return false;
 char path[80];snprintf(path,sizeof(path),"/proc/%d/mem",target_pid);
 int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return false;
 ssize_t n=pread(fd,buffer,size,(off_t)address);close(fd);
 protected_budget-=size;protected_reads++;
 return n==(ssize_t)size;
}
static long long now_ms() { timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
// Android 12 bionic: TLS slot 1 points at pthread_internal_t, whose tid and
// cached pid are at +0x10/+0x14. Inspect only the selected thread's final 64 KiB.
static uint64_t find_thread_tls(uint64_t lo,uint64_t hi,int pid,int tid) {
 if(!lo||hi<=lo)return 0;
 if(hi-lo>65536)lo=hi-65536;
 std::vector<unsigned char> bytes(hi-lo);
 if(!read_only(lo,bytes.data(),bytes.size()))return 0;
 uint64_t result=0;
 for(size_t i=0;i+24<bytes.size();i+=16) {
  uint32_t id=0,cached=0;memcpy(&id,bytes.data()+i+16,4);memcpy(&cached,bytes.data()+i+20,4);
  if(id!=(uint32_t)tid||(cached&0x7fffffff)!=(uint32_t)pid)continue;
  uint64_t thread=lo+i;
  for(size_t j=i+24;j+56<bytes.size();j+=8) {
   uint64_t pointer=0,bionic=0,guard=0;
   memcpy(&pointer,bytes.data()+j,8);if(pointer!=thread)continue;
   uint64_t tp=lo+j-8;if(tp%16)continue;
   memcpy(&bionic,bytes.data()+j-16,8);memcpy(&guard,bytes.data()+j+32,8);
   if(bionic<=tp||bionic>=hi||!guard)continue;
   if(result&&result!=tp)return 0;
   result=tp;
  }
 }
 return result;
}
static int choose_game_thread(int pid) {
 char tasks[80];snprintf(tasks,sizeof(tasks),"/proc/%d/task",pid);
 DIR *d=opendir(tasks);if(!d)return 0;dirent *e;uint64_t highest=0;int selected=0;
 while((e=readdir(d))) {
  int tid=atoi(e->d_name);if(tid<=0)continue;
  char path[128],name[64]={},stat[2048]={};
  snprintf(path,sizeof(path),"/proc/%d/task/%d/comm",pid,tid);
  FILE *f=fopen(path,"r");if(f){fgets(name,sizeof(name),f);fclose(f);}
  if(strncmp(name,"GameThread",10))continue;
  snprintf(path,sizeof(path),"/proc/%d/task/%d/stat",pid,tid);
  f=fopen(path,"r");if(!f)continue;fgets(stat,sizeof(stat),f);fclose(f);
  char *end=strrchr(stat,')');if(!end)continue;
  std::istringstream fields(end+1);std::string field;uint64_t ticks=0;
  for(int index=0;index<=12&&fields>>field;index++)if(index==11||index==12)ticks+=strtoull(field.c_str(),nullptr,10);
  if(!selected||ticks>highest){selected=tid;highest=ticks;}
 }
 closedir(d);return selected;
}
static bool initialize(int pid,uintptr_t base) {
 protected_begin=protected_end=0;protected_budget=0;
 target_pid=pid;module_base=base;emulated_target_pid=pid;
 emulated_target_tid=choose_game_thread(pid);if(!emulated_target_tid)return false;
 uintptr_t hook=0;if(!read_only(base+0x107bbb98,&hook,8))return false;hook&=0x00ffffffffffffffULL;
 char path[80],line[512];snprintf(path,sizeof(path),"/proc/%d/maps",pid);FILE *f=fopen(path,"r");if(!f)return false;
 GameOffsets offsets={};offsets.moduleBase=base;protected_begin=protected_end=0;
 uintptr_t stack_lo=0,stack_hi=0;char stack_label[80];snprintf(stack_label,sizeof(stack_label),"[anon:stack_and_tls:%d]",emulated_target_tid);
 while(fgets(line,sizeof(line),f)) {
  unsigned long lo,hi;char perms[5]={0};
  if(sscanf(line,"%lx-%lx %4s",&lo,&hi,perms)!=3)continue;
  if(strstr(line,stack_label)&&perms[0]=='r'){stack_lo=lo;stack_hi=hi;}
  if(hook>=lo&&hook<hi&&perms[0]=='r'&&perms[2]=='x') {offsets.shellcodeBase=lo;offsets.shellcodeSize=hi-lo;}
  if(base+0x2070b970>=lo&&base+0x2070b990<=hi&&perms[0]=='-'&&perms[1]=='w'&&
     strstr(line,"[anon:.bss]")&&hi-lo<=0x1000000) {protected_begin=lo;protected_end=hi;}
 }
 fclose(f);
 if(!offsets.shellcodeBase||offsets.shellcodeSize>0x400000)return false;
 offsets.scDecryptFuncOff=hook-offsets.shellcodeBase;SetMemoryReadFunc(read_only);
 ready=InitEmulatorARM64(pid,offsets);
 uint64_t tls=find_thread_tls(stack_lo,stack_hi,pid,emulated_target_tid);
 if(ready&&tls)SetTpidrEL0(tls);
 else {ready=false;CleanupEmulator();return false;}
 fprintf(stderr,"[decoder] local emulator initialized=%d thread=%d\n",ready,emulated_target_tid);
 return ready;
}
struct Cached {float p[3];long long at;};
static std::unordered_map<uint64_t,Cached> cache;
static int decode_now(int pid,uint64_t base,uint64_t root,float out[3],
                      PositionSource source=PositionSource::Transform,long long *sampled_at=nullptr) {
 if(!root||(root&7))return 0;
 if(target_pid!=pid||module_base!=base){cache.clear();ready=false;initialize_retry_at=0;}
 if(!ready) {
  if(now_ms()<initialize_retry_at)return 0;
  if(!initialize(pid,base)){initialize_retry_at=now_ms()+1000;return 0;}
  initialize_retry_at=0;
 }
 uint64_t key=decode_key(root,source);
 auto found=cache.find(key);long long now=now_ms();
 if(found!=cache.end()&&now-found->second.at<80){
  memcpy(out,found->second.p,12);if(sampled_at)*sampled_at=found->second.at;return 1;
 }
 InvalidatePageCache();protected_budget=256*1024;protected_reads=0;
 uc_engine *uc=GetEmulatorUc();unsigned char blank[0x200]={};
 auto call_getter=[&]() {
  if(source==PositionSource::CharacterBounds) {
   // Internal implementation called by SDK GetComponentBounds. It reads the
   // root's FEncBoxSphereBounds (+0x148), using the vector accessor (+0x2070b990).
   if(uc_mem_write(uc,GUEST_RESULT,blank,64)!=UC_ERR_OK)return false;
   CallARM64(base+0x1ae7cdf0,4,root,GUEST_RESULT,GUEST_RESULT+12,GUEST_RESULT+24);
  } else {
   // Preserve the existing loot transform path, including nested FEncVector.
   if(uc_mem_write(uc,GUEST_FRAME,blank,sizeof(blank))!=UC_ERR_OK)return false;
   CallARM64(base+0x1340b508,3,root,GUEST_FRAME,GUEST_RESULT);
  }
  uint64_t pc=0;
  return uc_reg_read(uc,UC_ARM64_REG_PC,&pc)==UC_ERR_OK&&
   (pc==0x7100000000ULL||pc==0x7100000004ULL);
 };
 long long started=now_ms();
 if(!call_getter())return 0;
 const long long snapshot_at=now_ms();
 if(!RefreshMappedSnapshot()||!call_getter())return 0;
 uint64_t pc=0;uc_reg_read(uc,UC_ARM64_REG_PC,&pc);
 static long long last_report=0;
 if(now_ms()-last_report>5000) {
  fprintf(stderr,"[decoder] %s pc=%llx elapsed=%lldms protectedPages=%zu\n",
   source==PositionSource::CharacterBounds?"character bounds":"loot transform",
   (unsigned long long)pc,now_ms()-started,protected_reads);last_report=now_ms();
 }
 if(pc!=0x7100000000ULL&&pc!=0x7100000004ULL)return 0;
 if(source==PositionSource::CharacterBounds) {
  float bounds[7]={};
  if(uc_mem_read(uc,GUEST_RESULT,bounds,sizeof(bounds))!=UC_ERR_OK)return 0;
  for(float value:bounds)if(!std::isfinite(value))return 0;
  // Missing or implausibly sized character bounds must never produce a marker.
  if(bounds[3]<1||bounds[3]>300||bounds[4]<1||bounds[4]>300||
     bounds[5]<10||bounds[5]>300||bounds[6]<10||bounds[6]>500)return 0;
  for(int i=0;i<3;i++)out[i]=bounds[i];
 } else {
  float transform[12]={};
  if(uc_mem_read(uc,GUEST_RESULT,transform,sizeof(transform))!=UC_ERR_OK)return 0;
  float norm=0;
  for(int i=0;i<4;i++){if(!std::isfinite(transform[i]))return 0;norm+=transform[i]*transform[i];}
  if(norm<0.5f||norm>1.5f)return 0;
  for(int i=0;i<3;i++)out[i]=transform[i+4];
 }
 for(int i=0;i<3;i++)if(!std::isfinite(out[i])||std::fabs(out[i])>10000000)return 0;
 make_position_cache_room(cache,key,now_ms(),[](const Cached &v){return v.at;});
 cache[key]={{out[0],out[1],out[2]},snapshot_at};
 if(sampled_at)*sampled_at=snapshot_at;
 return 1;
}

// Camera collection must never wait for an emulated transform. A single worker
// owns Unicorn; the scanner consumes bounded-age completed results.
static std::mutex result_mutex;
static std::condition_variable request_ready;
static DecodeSchedule schedule;
struct Completed {int pid;uint64_t base;Cached value;};
static std::unordered_map<uint64_t,Completed> completed;
static std::once_flag worker_once;
static void decoder_worker() {
 for(;;) {
  DecodeSchedule::Request r;
  {
   std::unique_lock<std::mutex> lock(result_mutex);
   for(;;) {
    request_ready.wait(lock,[]{return schedule.has_queued();});
    auto next=schedule.take(now_ms());
    if(next){r=*next;break;}
   }
  }
  float p[3];long long sampled_at=0;
  bool ok=decode_now(r.pid,r.base,r.root,p,r.source,&sampled_at)!=0;
  uint64_t key=decode_key(r.root,r.source);
  {std::lock_guard<std::mutex> lock(result_mutex);
   if(schedule.finish(r,ok,now_ms())&&ok) {
    make_position_cache_room(completed,key,now_ms(),[](const Completed &v){return v.value.at;});
    completed[key]={r.pid,r.base,{{p[0],p[1],p[2]},sampled_at}};
   }
  }
 }
}
static int queued_position(int pid,uint64_t base,uint64_t root,float out[3],PositionSource source) {
 if(!root||(root&7))return 0;
 uint64_t key=decode_key(root,source);
 std::call_once(worker_once,[]{std::thread(decoder_worker).detach();});
 std::lock_guard<std::mutex> lock(result_mutex);long long now=now_ms();
 if(schedule.set_context(pid,base))completed.clear();
 auto found=completed.find(key);
 bool valid=found!=completed.end()&&found->second.pid==pid&&found->second.base==base&&now-found->second.value.at<750;
 bool refresh=!valid||now-found->second.value.at>=80;
 if(refresh&&schedule.request(root,source,now))request_ready.notify_one();
 if(!valid)return 0;
 memcpy(out,found->second.value.p,12);return 1;
}

extern "C" int delta_decode_position(int pid,uint64_t base,uint64_t root,float out[3]) {
 return queued_position(pid,base,root,out,PositionSource::Transform);
}
extern "C" int delta_decode_character_bounds(int pid,uint64_t base,uint64_t root,float out[3]) {
 return queued_position(pid,base,root,out,PositionSource::CharacterBounds);
}
