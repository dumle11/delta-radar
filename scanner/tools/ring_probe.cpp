#include "../decode_bridge.cpp"
#include "transform_ring.h"
int main(int argc,char **argv) {
 if(argc<5)return 2;int pid=atoi(argv[1]);uint64_t base=strtoull(argv[2],nullptr,16);
 int count=atoi(argv[3]);if(count<1||count>80)return 2;
 if(!initialize(pid,base))return 3;
 for(int j=0;j<count;j++)for(int i=4;i<argc;i++) {
  uint64_t root=strtoull(argv[i],nullptr,16);InvalidatePageCache();protected_budget=256*1024;
  uint64_t pointer=CallARM64(base+0x107bbb8c,1,root)&0x00ffffffffffffffULL;
  float slab[27][12]={},out[3]={};int first=-1;
  bool rd=pointer>13*48&&read_only(pointer-13*48,slab,sizeof(slab));
  bool ok=rd&&delta_transform_ring_center(slab,out,&first);
  printf("t=%lld root=%lx selected=%lx valid=%d first=%d center=%.3f %.3f %.3f\n",now_ms(),root,pointer,ok,first,out[0]/100,out[1]/100,out[2]/100);fflush(stdout);
  usleep(50000);
 }
}
