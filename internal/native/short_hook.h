#ifndef INTERNAL_SHORT_HOOK_H
#define INTERNAL_SHORT_HOOK_H
/* Some Vulkan entry points are 8-byte thunks. A 16-byte overwrite would corrupt
 * their neighbor. Use one branch to a nearby veneer and copy one non-PC-relative
 * instruction. Allocation uses hints only, never MAP_FIXED or replacement. */
static int ih_short_supported(uint32_t instruction) {
    return !((instruction&0x7c000000u)==0x14000000u || (instruction&0x1f000000u)==0x10000000u ||
        (instruction&0x3b000000u)==0x18000000u || (instruction&0xff000010u)==0x54000000u ||
        (instruction&0x7e000000u)==0x34000000u || (instruction&0x7e000000u)==0x36000000u);
}
static int ih_short_patch(void *target,void *hook,void **original,uint8_t *slot) {
    uint32_t instruction;memcpy(&instruction,target,4);if(!ih_short_supported(instruction)) return 0;
    long page_size=sysconf(_SC_PAGESIZE);uintptr_t page=(uintptr_t)target&~((uintptr_t)page_size-1);
    void *veneer=MAP_FAILED;
    for(int attempt=1;attempt<=30;attempt++) {
        intptr_t distance=(intptr_t)((attempt+1)/2)*0x800000*(attempt&1?1:-1);
        void *hint=(void*)(page+distance);
        void *candidate=mmap(hint,(size_t)page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(candidate==MAP_FAILED) continue;
        intptr_t delta=(intptr_t)candidate-(intptr_t)target;
        if(delta>=-0x8000000 && delta<0x8000000 && !(delta&3)) {veneer=candidate;break;}
        munmap(candidate,(size_t)page_size);
    }
    if(veneer==MAP_FAILED) return 0;
    uint32_t jump[2]={0x58000050u,0xd61f0200u};
    memcpy(veneer,jump,8);uintptr_t destination=(uintptr_t)hook;memcpy((uint8_t*)veneer+8,&destination,8);
    __builtin___clear_cache((char*)veneer,(char*)veneer+16);
    if(mprotect(veneer,(size_t)page_size,PROT_READ|PROT_EXEC)!=0) {munmap(veneer,(size_t)page_size);return 0;}
    memcpy(slot,&instruction,4);memcpy(slot+4,jump,8);uintptr_t resume=(uintptr_t)target+4;memcpy(slot+12,&resume,8);
    __builtin___clear_cache((char*)slot,(char*)slot+20);
    if(mprotect((void*)page,(size_t)page_size,PROT_READ|PROT_WRITE|PROT_EXEC)!=0) {munmap(veneer,(size_t)page_size);return 0;}
    *original=slot;
    intptr_t words=((intptr_t)veneer-(intptr_t)target)/4;
    uint32_t branch=0x14000000u|((uint32_t)words&0x03ffffffu);
    memcpy(target,&branch,4);__builtin___clear_cache((char*)target,(char*)target+4);
    mprotect((void*)page,(size_t)page_size,PROT_READ|PROT_EXEC);
    return 1;
}
#endif
