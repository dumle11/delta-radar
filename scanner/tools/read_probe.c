/* Bounded module-relative reads for offline inspection, no remote writes. */
#define main scanner_main
#include "../memscan.c"
#undef main
int main(int argc,char **argv) {
 game_pid=find_game_pid();use_vm_readv=1;if(game_pid<=0||parse_maps()!=0||argc<3)return 2;
 uint64_t off=strtoull(argv[1],0,16);size_t len=strtoul(argv[2],0,16);if(len>0x10000||off+len>ue4_end-ue4_base)return 3;
 void *buf=malloc(len);ssize_t n=read_mem(ue4_base+off,buf,len);if(n<=0)return 4;fwrite(buf,1,n,stdout);return 0;
}
