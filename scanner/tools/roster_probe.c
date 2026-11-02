/* SDK-bounded world/subsystem and actor-roster diagnostics. */
#define main scanner_main
#include "../memscan.c"
#undef main
static int matches_class(uint64_t obj,const char *wanted) {
 uint64_t cls=read_ptr(obj+8);
 for(int i=0;i<12&&is_any_ptr(cls);i++) {char n[96];if(resolve_class_name(cls,n,sizeof(n))&&!strcmp(n,wanted))return 1;cls=read_ptr(cls+0x40);}
 return 0;
}
static void candidate(uint64_t p,uint64_t pc,int wo,int ro) {
 if(!is_any_ptr(p))return;
 if(read_ptr(p+0x88)!=pc&&!matches_class(p,"GPBattleFieldSystem"))return;
 printf("CANDIDATE %lx worldOff=%x refOff=%x pc=%lx pawn=%lx count=%d data=%lx\n",p,wo,ro,read_ptr(p+0x88),read_ptr(p+0x90),read_i32(p+0x118),read_ptr(p+0x110));
 uint64_t data=read_ptr(p+0x110);int n=read_i32(p+0x118);if(n<1||n>512)return;
 for(int i=0;i<n;i++) {uint64_t a=read_ptr(data+i*8),r=read_ptr(a+0x180),ps=read_ptr(a+0x390);char name[80]={0};read_fstring(ps+0x388,name,sizeof(name));printf(" member %lx root=%lx ps=%lx flags=%x/%x hidden=%x collision=%x name=%s pos148=%.2f,%.2f,%.2f pos220=%.2f,%.2f,%.2f q=%.2f %.2f %.2f %.2f\n",a,r,ps,read_i32(a+OFF_CHARACTER_LAST_IS_DEAD),read_i32(ps+OFF_PLAYER_STATE_DEAD),read_i32(a+0x90)&255,read_i32(a+0x95)&255,name,read_float(r+0x148)/100,read_float(r+0x14c)/100,read_float(r+0x150)/100,read_float(r+0x220)/100,read_float(r+0x224)/100,read_float(r+0x228)/100,read_float(r+0x210),read_float(r+0x214),read_float(r+0x218),read_float(r+0x21c));}
}
int main(void) {
 game_pid=find_game_pid();use_vm_readv=1;if(game_pid<=0||parse_maps()!=0)return 2;
 uint64_t w=read_ptr(ue4_base+OFF_GWORLD_FROM_UE4_BASE),gi=read_ptr(w+0x190),lp=read_ptr(read_ptr(gi+0x38)),pc=read_ptr(lp+0x30),pawn=read_ptr(pc+0x3f0);
 printf("pid=%d world=%lx pc=%lx pawn=%lx\n",game_pid,w,pc,pawn);
 uint64_t fields[0x830/8]={0};if(read_mem(w,fields,sizeof(fields))!=sizeof(fields))return 3;
 for(int i=0;i<(int)(sizeof(fields)/8);i++) {
  uint64_t p=fields[i];if(!is_game_heap_ptr(p))continue;candidate(p,pc,i*8,-1);
  if(i*8<0x230)continue;
  uint64_t refs[0x1000/8]={0};ssize_t n=read_mem(p,refs,sizeof(refs));if(n<0)continue;
  for(int j=0;j<n/8;j++)if(is_game_heap_ptr(refs[j]))candidate(refs[j],pc,i*8,j*8);
 }
 uint64_t level=read_ptr(w+0xf8),data=read_ptr(level+0x98);int n=read_i32(level+0xa0);if(n>4096)n=4096;
 for(int i=0;i<n;i++){uint64_t a=read_ptr(data+i*8);if(!is_game_heap_ptr(a))continue;if(matches_class(a,"GPSceneActorReplicator")) {printf("replicator=%lx\n",a);candidate(read_ptr(a+0x6f8),pc,-2,-2);}}
 return 0;
}
