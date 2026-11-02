/* SDK-bounded rendering-cache diagnostic; no game writes. */
#define main scanner_main
#include "../memscan.c"
#undef main
static void pos(const char *label,uint64_t a){printf(" %s=%.3f,%.3f,%.3f",label,read_float(a)/100,read_float(a+4)/100,read_float(a+8)/100);}
int main(int argc,char **argv){
 game_pid=find_game_pid();use_vm_readv=1;if(game_pid<=0||parse_maps()!=0)return 2;
 for(int i=1;i<argc&&i<30;i++){
  uint64_t a=strtoull(argv[i],NULL,16),r=read_ptr(a+0x180),m=read_ptr(a+0x3d0),ps=read_ptr(a+0x390);char cls[96]={0},name[96]={0};
  resolve_class_name(read_ptr(m+8),cls,sizeof(cls));read_fstring(ps+0x388,name,sizeof(name));
  printf("actor=%lx root=%lx mesh=%lx class=%s name=%s",a,r,m,cls,name);pos("root220",r+0x220);pos("mesh220",m+0x220);
  pos("cachedWorld",m+0x8e8);pos("extent",m+0x8f4);printf(" radius=%.3f",read_float(m+0x900)/100);puts("");
  uint64_t movement=read_ptr(a+0x3d8);printf("movement=%lx flags=%x floorDist=%.3f",movement,read_i32(movement+0x344)&255,read_float(movement+0x348)/100);pos("lastUpdate",movement+0x2a0);pos("floorLocation",movement+0x360);pos("floorImpact",movement+0x36c);pos("floorTraceStart",movement+0x390);puts("");
  float matrix[16]={};read_mem(m+0x910,matrix,sizeof(matrix));printf("inverse");for(int j=0;j<16;j++)printf(" %.4f",matrix[j]);puts("");
 }
 return 0;
}
