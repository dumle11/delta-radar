#!/usr/bin/env python3
"""Actual geometry helper: matrix projection and snapshot isolation."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
source=r'''
#include <vulkan/vulkan.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <assert.h>
#include <pthread.h>
#define MAX_ACTORS 4
#define MAX_SKELETON_BONES 4
#define LOOT_ITEM 1
typedef struct {
 float matrix[16],translation[3];VkViewport viewport;uint32_t width,height;int64_t recorded_at;unsigned serial;int valid,screen_rotation;
} GvFrame;
typedef struct {
 char id[32],kind[16];float x,y,z,half_height,health,max_health;
 int dead,team,is_player_bot,visibility_known,visible_from_camera,health_known,has_bones,bone_count;
 int bone_parent[4];float bone_pos[4][3];
 int loot_quality,loot_metadata_known,loot_value_known,loot_kind,loot_in_container,loot_color_known;
 uint64_t loot_unit_value;uint32_t loot_color;
} Entity;
static Entity entities[MAX_ACTORS];static int num_entities;
static char self_entity_id[32]="self";
static int64_t now=1000;static int64_t ef_now(void){return now;}
#include "esp_gpu.h"
int main(void) {
    GvFrame view={.width=1280,.height=720,.valid=1,.viewport={0,0,1280,720,0,1}};
    view.matrix[4]=1;view.matrix[9]=-1;view.matrix[14]=1;view.matrix[3]=1;
    float x,y;
    assert(eg_w2s(10,1,.5,&view,1280,720,&x,&y));
    assert(fabsf(x-704)<.001f && fabsf(y-342)<.001f);
    view.translation[0]=-100000;view.translation[1]=50000;view.translation[2]=-2000;
    assert(eg_w2s(1010,-499,20.5,&view,1280,720,&x,&y));
    assert(fabsf(x-704)<.001f && fabsf(y-342)<.001f);
    assert(!eg_w2s(990,-499,20.5,&view,1280,720,&x,&y));
    memset(view.translation,0,sizeof(view.translation));
    view.viewport.y=720;view.viewport.height=-720;
    assert(eg_w2s(10,1,.5,&view,1280,720,&x,&y) && fabsf(y-378)<.001f);
    view.viewport.y=0;view.viewport.height=720;
    num_entities=1;Entity *e=&entities[0];strcpy(e->id,"enemy");strcpy(e->kind,"p");
    e->x=10;e->half_height=1;e->visibility_known=1;e->visible_from_camera=1;e->health_known=1;e->health=50;e->max_health=100;
    e->has_bones=1;e->bone_count=2;e->bone_parent[0]=-1;e->bone_parent[1]=0;
    e->bone_pos[0][0]=e->bone_pos[1][0]=10;e->bone_pos[0][2]=-1;e->bone_pos[1][2]=1;
    atomic_store(&eg_esp_on,1);atomic_store(&eg_feat[0],1|2|4|16|64);atomic_store(&eg_rdist[0],500);
    atomic_store(&eg_palette[0][0],0xffff0000);atomic_store(&eg_palette[0][1],0xff00ff00);
    eg_publish(255,0,3);
    EgVert *a=calloc(EG_MAX_VERTS,sizeof(EgVert)),*b=calloc(EG_MAX_VERTS,sizeof(EgVert));
    int count=eg_draw(a,1280,720,&view);assert(count==42 && atomic_load(&eg_active));
    // A bot player shares enemy/team styling even when the retired bot slot is disabled.
    strcpy(e->kind,"a");e->is_player_bot=1;atomic_store(&eg_feat[3],0);eg_publish(255,0,3);
    assert(eg_draw(b,1280,720,&view)==count && !memcmp(a,b,count*sizeof(EgVert)));
    e->team=1;atomic_store(&eg_feat[1],1|2|4|16|64);atomic_store(&eg_rdist[1],500);atomic_store(&eg_palette[1][1],0xff44aaff);eg_publish(255,0,3);
    assert(eg_draw(b,1280,720,&view)==count && b[0].color==eg_argb_to_rgba(0xff44aaff));
    e->is_player_bot=0;eg_publish(255,0,3);assert(eg_draw(b,1280,720,&view)==0); // World NPC keeps its own disabled category.
    strcpy(e->kind,"p");e->team=2;eg_publish(255,0,3);
    // Transient scanner clearing/decryption/visibility changes cannot affect a published frame.
    e->dead=1;e->x=NAN;e->visible_from_camera=0;e->visibility_known=0;
    assert(eg_draw(b,1280,720,&view)==count && !memcmp(a,b,count*sizeof(EgVert)));
    // A new render matrix moves the geometry immediately, without a scanner publication.
    view.matrix[12]=100;
    assert(eg_draw(b,1280,720,&view)==count && fabsf(b[0].x-a[0].x-64)<.001f);
    assert(b[0].color==eg_argb_to_rgba(0xff00ff00));
    now+=201;assert(!eg_draw(b,1280,720,&view) && !atomic_load(&eg_active));
    free(a);free(b);puts("ESP GPU: submitted-matrix projection, pretranslation, viewport flip, snapshot/color isolation and per-frame movement passed");
}
'''
ndk=Path(os.environ.get('NDK_HOME','/opt/homebrew/share/android-ndk'))
includes=next((ndk/'toolchains/llvm/prebuilt').glob('*/sysroot/usr/include'))
with tempfile.TemporaryDirectory() as d:
 c=Path(d)/'test.c';binary=Path(d)/'test';c.write_text(source)
 subprocess.run(['cc','-std=gnu11','-I',str(root),'-idirafter',str(includes),str(c),'-pthread','-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
