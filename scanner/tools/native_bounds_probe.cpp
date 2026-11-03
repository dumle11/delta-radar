/* Read-only execution of the SDK's actual bounds accessor in local Unicorn. */
#include "../decode_bridge.cpp"
int main(int argc,char **argv){
 if(argc<4)return 2;int pid=atoi(argv[1]);uint64_t base=strtoull(argv[2],nullptr,16);
 if(!initialize(pid,base))return 3;
 protected_budget=256*1024;uint64_t getter=0;read_only(base+0x2070b990,&getter,8);
 unsigned char code[16]={};read_only(getter,code,sizeof(code));printf("vectorGetter=%lx code=",getter);for(unsigned char c:code)printf("%02x",c);puts("");
 for(int repeat=0;repeat<4;repeat++)for(int i=3;i<argc;i++){
  uint64_t component=strtoull(argv[i],nullptr,16);float raw[3]={},v[7]={};read_only(component+0x148,raw,12);
  InvalidatePageCache();protected_budget=256*1024;uc_engine* uc=GetEmulatorUc();unsigned char blank[64]={};uc_mem_write(uc,GUEST_RESULT,blank,sizeof(blank));
  CallARM64(base+0x1ae7cdf0,4,component,GUEST_RESULT,GUEST_RESULT+12,GUEST_RESULT+24);
  uint64_t pc=0;uc_reg_read(uc,UC_ARM64_REG_PC,&pc);uc_mem_read(uc,GUEST_RESULT,v,sizeof(v));
  printf("component=%lx repeat=%d pc=%lx raw=%.3f %.3f %.3f bounds=%.3f %.3f %.3f extent=%.3f %.3f %.3f radius=%.3f\n",component,repeat,pc,raw[0]/100,raw[1]/100,raw[2]/100,v[0]/100,v[1]/100,v[2]/100,v[3]/100,v[4]/100,v[5]/100,v[6]/100);fflush(stdout);
 }
}
