/* Read-only integration with the GPL-2.0 decoder in vendor/dfm-decoder.
 * All writes and simulated system calls are confined to Unicorn memory. */
#include "vendor/dfm-decoder/sjz_dec_arm64.h"
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
#include <deque>
extern int emulated_target_pid,emulated_target_tid;
static int target_pid=0;
static uintptr_t module_base=0;
static bool ready=false;
static bool read_only(uintptr_t address,void *buffer,size_t size) {
 address&=0x00ffffffffffffffULL;
 iovec local={buffer,size},remote={(void*)address,size};
 return syscall(__NR_process_vm_readv,target_pid,&local,1,&remote,1,0)==(ssize_t)size;
}
static long long now_ms() { timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (long long)t.tv_sec*1000+t.tv_nsec/1000000; }
static bool initialize(int pid,uintptr_t base) {
 target_pid=pid;module_base=base;emulated_target_pid=pid;emulated_target_tid=pid;
 char tasks[80];snprintf(tasks,sizeof(tasks),"/proc/%d/task",pid);DIR *d=opendir(tasks);dirent *e;
 while(d&&(e=readdir(d))) {char fn[128],name[64]={0};snprintf(fn,sizeof(fn),"/proc/%d/task/%s/comm",pid,e->d_name);FILE *f=fopen(fn,"r");if(f){fgets(name,sizeof(name),f);fclose(f);}if(strstr(name,"GameThread"))emulated_target_tid=atoi(e->d_name);}if(d)closedir(d);
 uintptr_t hook=0;if(!read_only(base+0x107bbb98,&hook,8))return false;hook&=0x00ffffffffffffffULL;
 char path[80],line[512];snprintf(path,sizeof(path),"/proc/%d/maps",pid);FILE *f=fopen(path,"r");if(!f)return false;
 GameOffsets offsets={};offsets.moduleBase=base;
 while(fgets(line,sizeof(line),f)){unsigned long lo,hi;char perms[5]={0};if(sscanf(line,"%lx-%lx %4s",&lo,&hi,perms)==3&&hook>=lo&&hook<hi&&perms[0]=='r'&&perms[2]=='x'){offsets.shellcodeBase=lo;offsets.shellcodeSize=hi-lo;break;}}fclose(f);
 if(!offsets.shellcodeBase||offsets.shellcodeSize>0x400000)return false;
 offsets.scDecryptFuncOff=hook-offsets.shellcodeBase;SetMemoryReadFunc(read_only);
 ready=InitEmulatorARM64(pid,offsets);
 fprintf(stderr,"[decoder] local emulator initialized=%d thread=%d\n",ready,emulated_target_tid);
 return ready;
}
struct Cached {float p[3];long long at;};
static std::unordered_map<uint64_t,Cached> cache;
static int decode_now(int pid,uint64_t base,uint64_t root,float out[3]) {
 if(target_pid!=pid||module_base!=base){cache.clear();ready=false;}
 if(!ready&&!initialize(pid,base))return 0;
 auto found=cache.find(root);long long now=now_ms();
 if(found!=cache.end()&&now-found->second.at<80){memcpy(out,found->second.p,12);return 1;}
 InvalidatePageCache();
 uint64_t result=CallARM64(base+0x107bbb8c,1,(uint64_t)root),pc=0;
 uc_engine *uc=GetEmulatorUc();uc_reg_read(uc,UC_ARM64_REG_PC,&pc);
 if(pc!=0x7100000000ULL&&pc!=0x7100000004ULL)return 0;
 float transform[12]={};
 if(uc_mem_read(uc,result,transform,sizeof(transform))!=UC_ERR_OK) {
  // The engine can return an already-decoded remote cache without touching its
  // page during emulation. Copy the returned value through our read-only path.
  if(!read_only(result,transform,sizeof(transform)))return 0;
 }
 float norm=0;for(int i=0;i<4;i++){if(!std::isfinite(transform[i]))return 0;norm+=transform[i]*transform[i];}
 if(norm<0.5f||norm>1.5f)return 0;
 for(int i=0;i<3;i++){out[i]=transform[i+4];if(!std::isfinite(out[i])||std::fabs(out[i])>10000000)return 0;}
 if(cache.size()>512)cache.clear();Cached next={{out[0],out[1],out[2]},now};cache[root]=next;
 return 1;
}

// Camera collection must never wait for an emulated transform. A single worker
// owns Unicorn; the scanner consumes bounded-age completed results.
struct Request {int pid;uint64_t base,root;};
static std::mutex result_mutex;
static std::condition_variable request_ready;
static std::deque<Request> requests;
struct Completed {int pid;uint64_t base;Cached value;};
static std::unordered_map<uint64_t,Completed> completed;
static std::unordered_map<uint64_t,bool> pending;
static std::once_flag worker_once;
static void decoder_worker() {
 for(;;) {
  Request r;
  {std::unique_lock<std::mutex> lock(result_mutex);request_ready.wait(lock,[]{return !requests.empty();});r=requests.front();requests.pop_front();}
  float p[3];bool ok=decode_now(r.pid,r.base,r.root,p)!=0;
  {std::lock_guard<std::mutex> lock(result_mutex);
   if(ok) completed[r.root]={r.pid,r.base,{{p[0],p[1],p[2]},now_ms()}};
   pending.erase(r.root);
   if(completed.size()>512)completed.clear();
  }
 }
}
extern "C" int delta_decode_position(int pid,uint64_t base,uint64_t root,float out[3]) {
 std::call_once(worker_once,[]{std::thread(decoder_worker).detach();});
 std::lock_guard<std::mutex> lock(result_mutex);long long now=now_ms();
 auto found=completed.find(root);
 bool valid=found!=completed.end()&&found->second.pid==pid&&found->second.base==base&&now-found->second.value.at<750;
 bool refresh=!valid||now-found->second.value.at>=80;
 if(refresh&&!pending.count(root)&&requests.size()<64) {requests.push_back({pid,base,root});pending[root]=true;request_ready.notify_one();}
 if(!valid)return 0;
 memcpy(out,found->second.value.p,12);return 1;
}
