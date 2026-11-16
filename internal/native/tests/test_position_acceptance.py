"""Production internal position acceptance with independently tagged bounds/transform."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[3]
s=(root/'external/scanner/memscan-legacy.c').read_text()
source=s[s.index('static unsigned position_unverified_rejections'):s.index('static int actor_rendered_on_screen(')]
program=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#define BUILD_AS_LIBRARY 1
#define OFF_PROXIMA_ROOT_WORLD_LOCATION 0x148
typedef struct {uint64_t root;int present,decoded,bounds_index,bounds_encrypted,bounds_flags,world_index,world_encrypted,world_flags;float bounds[3];} PositionObservation;
static unsigned char memory[1024];static int decode,native;
static int internal_actor_position(uint64_t actor,uint64_t root,float *out) {(void)actor;(void)root;if(!native)return 0;out[0]=1000;out[1]=2000;out[2]=3000;return 1;}
static ssize_t read_mem(uint64_t a,void *p,size_t n) {if(a<(uintptr_t)memory || a+n>(uintptr_t)(memory+sizeof(memory))) return -1;memcpy(p,(void*)a,n);return n;}
static int enc_handler_unmanaged(const uint8_t *p) {return p[0]==255&&p[1]==255&&!p[2];}
static int position_decode_actor(uint64_t actor,uint64_t component,float *out) {(void)actor;(void)component;if(!decode)return 0;out[0]=1000;out[1]=2000;out[2]=3000;return 1;}
static void f(int offset,float value) {memcpy(memory+offset,&value,4);}
''' +source+r'''
int main(void) {
 uint64_t component=(uintptr_t)memory;float out[3];int plain;PositionObservation observation;
 f(0x21c,1);f(0x220,1000);f(0x224,2000);f(0x228,3000);memory[0x238]=memory[0x239]=255;
 f(0x148,1000);f(0x14c,2000);f(0x150,3000);memory[0x154]=memory[0x155]=255;
 assert(resolve_actor_position(1,component,0x148,out,&plain,&observation)&&plain&&out[0]==1000);
 // Independently protected bounds must use the verified world transform.
 memory[0x154]=12;memory[0x156]=1;f(0x148,123456);
 assert(resolve_actor_position(1,component,0x148,out,&plain,&observation)&&plain&&out[0]==1000);
 assert(position_world_fallbacks==1&&!observation.decoded);
 // Both protected: finite raw bytes do not establish a world position.
 memory[0x238]=12;memory[0x23a]=1;
 assert(!resolve_actor_position(1,component,0x148,out,&plain,&observation)&&!plain);
 assert(position_unverified_rejections==1 && position_frame_failures==1);
 position_frame_failures=0;
 decode=1;
 assert(resolve_actor_position(1,component,0x148,out,&plain,&observation)&&plain&&observation.decoded&&out[0]==1000);
 assert(position_decode_successes==1);
 decode=0;native=1;
 assert(resolve_actor_position(1,component,0x148,out,&plain,&observation)&&plain&&observation.decoded);
 assert(position_native_successes==1 && position_frame_failures==0);
 puts("PASS: independent tags, plain-transform fallback, protected-coordinate rejection, verified decoding");
}
'''
with tempfile.TemporaryDirectory() as d:
 src=Path(d)/'position.c';binary=Path(d)/'position';src.write_text(program)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra',str(src),'-lm','-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
