"""Actual CHARACTER/Pawn ancestry wins over capsule-shaped spawn/navigation actors."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'external/scanner/memscan-legacy.c').read_text()
source=s[s.index('static int is_character_class_uncached('):s.index('static int pawn_is_controlled(')]
program=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#define OFF_UOBJECT_NAME 0x1c
static unsigned char heap[4096];static uint64_t verified_uworld=1;
static int _icc_diag_done,_icc_diag_len;static char _icc_diag_buf[512];
static int is_any_ptr(uint64_t p) {return p>=(uintptr_t)heap && p<(uintptr_t)(heap+sizeof(heap));}
static ssize_t read_mem(uint64_t p,void *out,size_t n) {if(!is_any_ptr(p)||p+n>(uintptr_t)(heap+sizeof(heap))) return -1;memcpy(out,(void*)p,n);return n;}
static int32_t read_i32(uint64_t p) {int32_t n=0;read_mem(p,&n,4);return n;}
static void resolve_fname(int32_t id,char *out,int size) {const char *names[]={"","Pawn","CHARACTER","PlayerStart","Actor","BP_Operator_C","DFMAICharacter"};snprintf(out,size,"%s",id>=0&&id<7?names[id]:"");}
static void put32(uint64_t p,int n) {memcpy((void*)p,&n,4);}static void put64(uint64_t p,uint64_t n) {memcpy((void*)p,&n,8);}
''' +source+r'''
int main(void) {
 uint64_t pawn=(uintptr_t)heap+128,character=pawn+128,start=character+128,actor=start+128,bp=actor+128,ai=bp+128,unknown=ai+128;
 put32(pawn+0x1c,1);put32(pawn+0x24,0xd8);put32(character+0x1c,2);put32(character+0x24,0xda);put64(character+0x40,pawn);
 put32(start+0x1c,3);put32(start+0x24,0xaa1);put64(start+0x40,actor);put32(actor+0x1c,4);
 put32(bp+0x1c,5);put64(bp+0x40,character);put32(ai+0x1c,6);put64(ai+0x40,character);
 assert(is_character_class(character,0) && is_character_class(bp,0) && is_character_class(ai,0));
 assert(!is_character_class(start,0)); // PlayerStart has a capsule, but isn't a pawn/character.
 put32(start+0x24,0xda);put32(actor+0x24,0xd8);assert(!is_character_class(start,0)); // A conflicting numeric pair can't override its known class.
 put32(unknown+0x24,0xda);put64(unknown+0x40,pawn);assert(is_character_class(unknown,0)); // Unreadable name, verified native pair.
 put32(start+0x1c,5);put64(start+0x40,character);assert(is_character_class(start,0)); // Recycled address/name invalidates cache.
 put64(start+0x40,actor);++verified_uworld;assert(!is_character_class(start,0));
 assert(!is_character_class(0,0));
 puts("PASS: operators/NPCs, PlayerStart rejection, native-name checks, class recycling and world cache invalidation");
}
'''
with tempfile.TemporaryDirectory() as directory:
 src=Path(directory)/'class.c';binary=Path(directory)/'class';src.write_text(program)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra',str(src),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
