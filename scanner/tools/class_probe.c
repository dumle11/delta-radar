/* Small read-only probe for SDK class/object identities and the local chain. */
#define main scanner_main
#include "../memscan.c"
#undef main
static void chain(const char *label,uint64_t obj) {
    uint64_t cls=read_ptr(obj+8);
    printf("%s obj=%lx index=%d class=%lx\n",label,obj,read_i32(obj+0x24),cls);
    for(int i=0;i<16&&is_any_ptr(cls);i++) {
        printf(" class %lx internal=%d fname=%d super=%lx\n",cls,read_i32(cls+0x24),read_i32(cls+0x1c),read_ptr(cls+0x40));
        uint64_t next=read_ptr(cls+0x40);if(next==cls)break;cls=next;
    }
}
int main(void) {
    game_pid=find_game_pid();use_vm_readv=1;if(game_pid<=0||parse_maps()!=0)return 2;
    uint64_t w=read_ptr(ue4_base+OFF_GWORLD_FROM_UE4_BASE),gi=read_ptr(w+0x190),lp=read_ptr(read_ptr(gi+0x38)),pc=read_ptr(lp+0x30),pawn=read_ptr(pc+0x3f0),root=read_ptr(pawn+0x180),state=read_ptr(pawn+0x390);
    printf("pid=%d base=%lx world=%lx\n",game_pid,ue4_base,w);
    chain("pawn",pawn);chain("root",root);chain("state",state);chain("camera",read_ptr(pc+0x408));
    char name[80]={0};read_fstring(state+0x388,name,sizeof(name));printf("name388=%s ",name);read_fstring(state+0x470,name,sizeof(name));printf("name470=%s team=%d hh=%.2f\n",name,read_i32(state+0x658),read_float(root+0x5c0));
    uint64_t hc=read_ptr(pawn+0x10b8),hs=read_ptr(hc+0x280);printf("healthcomp=%lx healthset=%lx health=%.2f max=%.2f alive=%.2f\n",hc,hs,read_float(hs+0x3c),read_float(hs+0x54),read_float(hs+0x134));
    return 0;
}
