"""Semantic aim joints and strict selected-joint visibility precedence."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[3]
skeleton=(root/'external/scanner/skeleton-reader.h').read_text()
start=skeleton.index('static int skeleton_aim_name_rank(')
end=skeleton.index('\n}',start)+2
program=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#define MAX_SKELETON_BONES 128
typedef struct {float x,y,z;unsigned aim_bone_mask;float aim_bone_pos[3][3];int has_bones,bone_count;float bone_pos[128][3];} Entity;
#include "aim_target.h"
''' + skeleton[start:end] + r'''
static int calls,first_result;
static int query(void *context,const float *point) {(void)context;(void)point;return calls++?0:first_result;}
int main(void) {
 assert(skeleton_aim_name_rank("Head",0)>0 && skeleton_aim_name_rank("Bip001_Head",0)>0);
 assert(!skeleton_aim_name_rank("hand_l",0) && !skeleton_aim_name_rank("upperarm_r",0));
 assert(skeleton_aim_name_rank("spine_03",1)>0 && skeleton_aim_name_rank("pelvis",2)>0);
 Entity e={.x=10,.y=20,.z=1,.aim_bone_mask=7,.has_bones=1,.bone_count=17};float out[3];
 float positions[3][3]={{10.1f,20.2f,1.8f},{10,20,.9f},{10,20,.7f}};
 memcpy(e.aim_bone_pos,positions,sizeof(positions));
 e.bone_pos[4][0]=30;e.bone_pos[4][1]=20;e.bone_pos[4][2]=1.5f; // A drawing-list arm must never become "Head".
 assert(aim_entity_point(&e,0,out) && fabsf(out[0]-1010)<.01 && fabsf(out[2]-180)<.01);
 e.aim_bone_pos[0][2]=1.1f;assert(aim_entity_point(&e,0,out) && fabsf(out[2]-110)<.01); // Crouch.
 e.aim_bone_pos[0][0]=10.5f;assert(aim_entity_point(&e,0,out) && out[0]==1050); // Lean.
 first_result=0;calls=0;assert(aim_visible_entity_point(&e,0,out,query,0) && calls==1 && out[0]==1050);
 first_result=1;calls=0;assert(aim_visible_entity_point(&e,0,out,query,0) && calls==2 && out[2]==90);
 first_result=255;calls=0;assert(!aim_visible_entity_point(&e,0,out,query,0) && calls==1);
 e.aim_bone_pos[0][2]=NAN;calls=0;assert(!aim_visible_entity_point(&e,0,out,query,0) && calls==0);
 e.aim_bone_mask=6;assert(!aim_entity_point(&e,0,out)); // Unknown selected bone doesn't silently aim at an arm.
 puts("PASS: bone-name semantics, crouch/lean, visible selected head always wins, blocked-only fallback, failed/unknown queries");
}
'''
with tempfile.TemporaryDirectory() as directory:
 src=Path(directory)/'target.c';binary=Path(directory)/'target';src.write_text(program)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-I',str(root/'internal/native'),str(src),'-lm','-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
