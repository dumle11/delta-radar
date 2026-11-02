/* Bounded comparison of live actor/root/mesh transforms; no game writes. */
#define main scanner_main
#include "../memscan.c"
#undef main
static void vector(const char *label,uint64_t addr) {printf(" %s %.3f %.3f %.3f",label,read_float(addr)/100,read_float(addr+4)/100,read_float(addr+8)/100);}
static void component(const char *label,uint64_t obj) {
 char name[96]={0};resolve_class_name(read_ptr(obj+8),name,sizeof(name));
 printf("%s %lx type=%s parent=%lx",label,obj,name,read_ptr(obj+0x108));
 vector("p148",obj+0x148);vector("rel168",obj+0x168);vector("p210",obj+0x210);vector("p220",obj+0x220);vector("p230",obj+0x230);puts("");
 unsigned char buf[0x600]={0};ssize_t n=read_mem(obj,buf,sizeof(buf));
 printf("raw %s %ld ",label,(long)n);for(int i=0;i<n;i++)printf("%02x",buf[i]);puts("");
}
int main(int argc,char **argv) {
 game_pid=find_game_pid();use_vm_readv=1;if(game_pid<=0||parse_maps()!=0)return 2;
 uint64_t w=read_ptr(ue4_base+OFF_GWORLD_FROM_UE4_BASE);
 printf("pid=%d world=%lx levels=%d data=%lx\n",game_pid,w,read_i32(w+0x160),read_ptr(w+0x158));
 uint64_t gi=read_ptr(w+0x190),lp=read_ptr(read_ptr(gi+0x38)),pc=read_ptr(lp+0x30),audio=read_ptr(pc+0x970);
 uint64_t shadows=read_ptr(audio+0x2d0);int sc=read_i32(audio+0x2d8);
 printf("audio=%lx shadows=%lx count=%d\n",audio,shadows,sc);
 if(sc>0&&sc<=512)for(int i=0;i<sc;i++)printf(" shadow %lx\n",read_ptr(shadows+i*8));
 for(int i=1;i<argc&&i<=12;i++) {
  uint64_t a=strtoull(argv[i],NULL,16),r=read_ptr(a+0x180),mesh=read_ptr(a+0x3d0),state=read_ptr(a+0x390);
  printf("actor %lx state=%lx currentPawn=%lx hidden=%d rootFlags=%x meshFlags=%x",a,state,read_ptr(state+0x3f8),read_i32(a+0x90)&1,read_i32(r+0x19c)&255,read_i32(mesh+0x19c)&255);vector("repB4",a+0xb4);puts("");
  component("root",r);if(is_any_ptr(mesh))component("mesh",mesh);
 }
 return 0;
}
