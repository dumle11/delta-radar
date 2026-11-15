"""Check the matching live FName format and reject false pool sentinels."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[3] / 'external/scanner/memscan-legacy.c').read_text()
start = source.index('static int resolve_fname(')
end = source.index('/* ========== RUNNING MODE', start)
program = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
static unsigned char heap[8192];
static uint64_t ue4_base=(uint64_t)heap;
#define OFF_GNAMES_FROM_UE4_BASE 128
#define OFF_FNAME_POOL_BLOCKS 0x38
static uint64_t fname_pool_addr;
static int fname_blocks_off=0x38, fname_entry_hdr_size=2, fname_encoded_ascii;
static int is_any_ptr(uint64_t p){return p>=(uint64_t)heap && p<(uint64_t)(heap+sizeof(heap));}
static long read_mem(uint64_t p,void *out,unsigned long n){
 if(!is_any_ptr(p)||n>(uint64_t)(heap+sizeof(heap))-p)return -1;
 memcpy(out,(void*)p,n);return n;
}
static uint64_t read_ptr(uint64_t p){uint64_t v=0;read_mem(p,&v,8);return v;}
static void put_name(int offset,const char *name,int key){
 uint16_t header=(uint16_t)(strlen(name)<<6)|0x2a;
 unsigned char *p=heap+1024+offset;
 memcpy(p,&header,2);
 for(unsigned i=0;i<strlen(name);i++)p[i+2]=(unsigned char)name[i]^key;
}
''' + source[start:end] + r'''
int main(void){
 uint64_t block=(uint64_t)heap+1024;
 memcpy(heap+128+0x38,&block,8);
 put_name(0,"None",0xFF);
 put_name(32,"DFMRangeTargetCharacter",0xFF);
 put_name(128,"BindSplineComponent",0x7F);
 char out[96];
 assert(try_sdk_fname_pool() && fname_encoded_ascii && fname_entry_hdr_size==2);
 assert(resolve_fname(0,out,sizeof(out))==4 && !strcmp(out,"None"));
 assert(resolve_fname(16,out,sizeof(out))==23 && !strcmp(out,"DFMRangeTargetCharacter"));
 assert(resolve_fname(64,out,sizeof(out))==19 && !strcmp(out,"BindSplineComponent"));
 assert(!resolve_fname(16,out,8));
 assert(!resolve_fname(-1,out,sizeof(out)));
 heap[1024+128]|=1;assert(!resolve_fname(64,out,sizeof(out))); /* Wide names remain unresolved. */
 put_name(0,"None",0);put_name(32,"StaticMeshComponent",0);
 assert(try_sdk_fname_pool() && !fname_encoded_ascii);
 assert(resolve_fname(16,out,sizeof(out))==19 && !strcmp(out,"StaticMeshComponent"));
 put_name(0,"Fake",0);assert(!try_sdk_fname_pool());
 return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'names.c'
    exe = Path(d) / 'names'
    c.write_text(program)
    subprocess.run(['cc','-O2',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: encoded/plain FNames, native class names, bounds, wide-name rejection, false sentinel rejection')
