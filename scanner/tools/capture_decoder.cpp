// Bounded diagnostic: capture only the pages read by the existing decoder.
#include "../decode_bridge.cpp"
#include <sys/stat.h>
int main(int argc,char **argv) {
 if(argc!=6)return 2;
 int pid=atoi(argv[1]),count=atoi(argv[5]);
 uint64_t base=strtoull(argv[2],nullptr,16),root=strtoull(argv[3],nullptr,16);
 if(count<1||count>8)return 2;
 const char *folder=argv[4];if(mkdir(folder,0700)&&errno!=EEXIST)return 3;
 EnableMachineCapture(true);
 for(int i=0;i<count;i++) {
  float out[3]={};int ok=decode_now(pid,base,root,out);char prefix[512];
  snprintf(prefix,sizeof(prefix),"%s/capture-%02d",folder,i);
  bool saved=SaveMachineCapture(prefix,root);
  printf("capture=%d ok=%d saved=%d pos=%.3f %.3f %.3f\n",i,ok,saved,out[0]/100,out[1]/100,out[2]/100);
  fflush(stdout);usleep(150000);
 }
 return 0;
}
