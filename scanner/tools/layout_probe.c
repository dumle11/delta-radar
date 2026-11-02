/* Bounded, read-only SDK layout diagnostics. Does not write to the game. */
#define main scanner_main
#include "../memscan.c"
#undef main

static void describe(const char *label, uint64_t obj) {
    char name[128]={0}, clsname[128]={0};
    uint64_t cls=read_ptr(obj+OFF_UOBJECT_CLASS);
    resolve_fname(read_i32(obj+OFF_UOBJECT_NAME),name,sizeof(name));
    resolve_fname(read_i32(cls+OFF_UOBJECT_NAME),clsname,sizeof(clsname));
    printf("%s=0x%lx class=0x%lx %s name=%s\n",label,(unsigned long)obj,(unsigned long)cls,clsname,name);
}
int main(int argc, char **argv) {
    game_pid=find_game_pid(); use_vm_readv=1;
    if(game_pid<=0 || parse_maps()!=0 || !ue4_base) return 2;
    printf("pid=%d base=0x%lx\n",game_pid,(unsigned long)ue4_base);
    for (int variant=0; variant<2;variant++) {
        uint64_t pool=ue4_base+(variant?OFF_GNAMES_READABLE_ALIAS:OFF_GNAMES_FROM_UE4_BASE);
        uint64_t block=read_ptr(pool+0x38); unsigned char b[16]={0};
        ssize_t n=read_mem(block,b,sizeof(b));
        printf("pool%d=0x%lx block=0x%lx n=%ld head=",variant,(unsigned long)pool,(unsigned long)block,(long)n);
        for(int i=0;i<16;i++) printf("%02x",b[i]); puts("");
        char decoded[4]; memcpy(decoded,b+2,4);decode_name_bytes(decoded,4);
        if(n==16 && !memcmp(decoded,"None",4)) { fname_pool_addr=pool;fname_blocks_off=0x38;fname_entry_hdr_size=2; }
    }
    uint64_t world=read_ptr(ue4_base+OFF_GWORLD_FROM_UE4_BASE); describe("world",world);
    uint64_t level=read_ptr(world+OFF_UWORLD_PERSISTENT_LEVEL); describe("level",level);
    uint64_t gi=read_ptr(world+0x190), lps=read_ptr(gi+0x38),lp=read_ptr(lps),pc=read_ptr(lp+0x30),pawn=read_ptr(pc+OFF_PLAYER_CONTROLLER_ACKNOWLEDGED_PAWN),root=read_ptr(pawn+OFF_AACTOR_ROOT_COMPONENT);
    describe("gi",gi);describe("pc",pc);describe("pawn",pawn);describe("root",root);
    uint64_t cam=read_ptr(pc+OFF_PC_CAMERA_MANAGER);describe("camera",cam);
    for(int off=0x100;off<0x210;off+=4) printf("root+%x %.4f\n",off,read_float(root+off));
    for(int off=0x80;off<0x300;off+=8) {
        if(!sane_tarray(level,off,50000))continue;
        int n=read_i32(level+off+8); if(n<5)continue;
        printf("level array+%x data=0x%lx count=%d\n",off,(unsigned long)read_ptr(level+off),n);
    }
    for(int k=0;k<2;k++) {
        uint64_t cache=cam+(k?0x37b0:0x2d60),pov=cache+0x10;
        printf("cache+%lx ts=%.4f loc=%.2f %.2f %.2f rot=%.2f %.2f fov=%.2f fpp=%.2f\n",cache-cam,read_float(cache),read_float(pov),read_float(pov+4),read_float(pov+8),read_float(pov+0x10),read_float(pov+0x14),read_float(pov+0x20),read_float(cam+OFF_GP_CAMERA_FPP_FOV));
    }
    printf("localplayer axis=%d\n",read_i32(lp+0x94)&255);
    for(int off=0;off<0x38;off+=4) printf("POV+%x %.5f\n",off,read_float(cam+0x2d70+off));
    if(argc>1) {
        uint64_t obj=strtoull(argv[1],NULL,16); describe("requested",obj);
        int end=argc>2?strtol(argv[2],NULL,16):0x200;
        if(end>0x10000)end=0x10000;
        for(int off=0;off<end;off+=8) printf("+%04x %016lx %.4f %.4f\n",off,(unsigned long)read_ptr(obj+off),read_float(obj+off),read_float(obj+off+4));
    }
    return 0;
}
