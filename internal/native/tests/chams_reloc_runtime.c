#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "chams_reloc.h"
int main(void) {
 uint32_t *code=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 uint32_t *tramp=mmap(0,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(code!=MAP_FAILED && tramp!=MAP_FAILED);
 const uint32_t original[]={0xf9400408,0x36000041,0x39210102,0x36080041,0x39210502,0x52800029,0xb9084909,0xd65f03c0};
 memcpy(code,original,sizeof(original));assert(cr_trampoline(tramp,code,(uintptr_t)code));
 __builtin___clear_cache((char*)code,(char*)code+4096);__builtin___clear_cache((char*)tramp,(char*)tramp+4096);
 assert(!mprotect(code,4096,PROT_READ|PROT_EXEC) && !mprotect(tramp,4096,PROT_READ|PROT_EXEC));
 unsigned char state[0x850];void *cb[2]={0,state};
 for(unsigned flags=0;flags<4;flags++) {
  memset(state,0,sizeof(state));((void(*)(void*,unsigned,unsigned))tramp)(cb,flags,123);
  assert(state[0x840]==((flags&1)?123:0));assert(state[0x841]==((flags&2)?123:0));assert(state[0x848]==1);
 }
 assert(!mprotect(code,4096,PROT_READ|PROT_WRITE) && !mprotect(tramp,4096,PROT_READ|PROT_WRITE));
 const uint32_t viewport[]={0xf9400408,0x0b010049,0x6b01013f,0x54000389};
 memcpy(code,viewport,sizeof(viewport));code[4]=0x52800020;code[5]=0xd65f03c0;
 code[31]=0x52800040;code[32]=0xd65f03c0;
 assert(cr_trampoline(tramp,code,(uintptr_t)code));
 __builtin___clear_cache((char*)code,(char*)code+4096);__builtin___clear_cache((char*)tramp,(char*)tramp+4096);
 assert(!mprotect(code,4096,PROT_READ|PROT_EXEC) && !mprotect(tramp,4096,PROT_READ|PROT_EXEC));
 const unsigned cases[][2]={{0,0},{0,1},{1,1},{9,0},{0xffffffffu,1},{3,0xfffffffeu}};
 for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
  unsigned first=cases[i][0],count=cases[i][1];
  unsigned expected=((unsigned)(first+count)<=first)?2:1;
  assert(((unsigned(*)(void*,unsigned,unsigned))tramp)(cb,first,count)==expected);
 }
 puts("ARM64 captured stencil/viewport trampolines: internal/external conditional branches passed");
}
