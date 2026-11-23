#!/usr/bin/env python3
"""Exercise the production native input ownership loop without a running game."""
from pathlib import Path
import os
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
header=(root/'vulkan_present.h').read_text()
function=header[header.index('static int32_t vp_get_event('):header.index('typedef struct {')]
source=r'''
#include <android/input.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdatomic.h>
#include <pthread.h>
struct AInputEvent {int type,source,action;float x,y;};
static AInputEvent events[10];static int at,count,finished,routed,route_ok=1;
static int vp_touch_owned;
static _Atomic int vp_active=1;
static pthread_mutex_t vp_pixels_mutex=PTHREAD_MUTEX_INITIALIZER;
static float vp_hits[12]={10,20,100,110};static unsigned vp_hit_count=1;
static int64_t vp_hit_at=1000;
static int64_t ef_now(void) {return 1000;}
static int vp_route(AInputEvent *event) {routed++;return route_ok;}
int32_t AInputEvent_getType(const AInputEvent *e) {return e->type;}
int32_t AInputEvent_getSource(const AInputEvent *e) {return e->source;}
int32_t AMotionEvent_getAction(const AInputEvent *e) {return e->action;}
float AMotionEvent_getX(const AInputEvent *e,size_t index) {return e->x;}
float AMotionEvent_getY(const AInputEvent *e,size_t index) {return e->y;}
void AInputQueue_finishEvent(AInputQueue *q,AInputEvent *e,int handled) {assert(handled==1);finished++;}
static int32_t next(AInputQueue *q,AInputEvent **event) {if(at==count) return -1;*event=&events[at++];return 0;}
static int32_t (*vp_o_get_event)(AInputQueue*,AInputEvent**)=next;
static void add(int action,float x,float y) {events[count++]=(AInputEvent){AINPUT_EVENT_TYPE_MOTION,AINPUT_SOURCE_TOUCHSCREEN,action,x,y};}
'''+function+r'''
int main(void) {
    AInputEvent *event=NULL;
    // A gesture that starts outside remains game-owned, even when moving over UI.
    add(AMOTION_EVENT_ACTION_DOWN,200,200);add(AMOTION_EVENT_ACTION_MOVE,50,50);add(AMOTION_EVENT_ACTION_UP,50,50);
    for(int i=0;i<3;i++) {assert(vp_get_event(NULL,&event)==0);assert(event==&events[i]);}
    assert(!finished && !routed);
    // UI drag remains UI-owned outside its rectangle, then releases ownership.
    at=count=0;add(AMOTION_EVENT_ACTION_DOWN,50,50);add(AMOTION_EVENT_ACTION_MOVE,200,200);add(AMOTION_EVENT_ACTION_UP,200,200);
    add(AMOTION_EVENT_ACTION_DOWN,300,300);
    assert(vp_get_event(NULL,&event)==0 && event==&events[3]);
    assert(finished==3 && routed==3 && !vp_touch_owned);
    // Failed delivery of a new gesture passes the original DOWN to the game.
    at=count=0;route_ok=0;add(AMOTION_EVENT_ACTION_DOWN,50,50);
    assert(vp_get_event(NULL,&event)==0 && event==&events[0] && !vp_touch_owned && finished==3);
    // Expired controls and a mouse source do not intercept gameplay.
    at=count=0;route_ok=1;vp_hit_at=0;add(AMOTION_EVENT_ACTION_DOWN,50,50);
    assert(vp_get_event(NULL,&event)==0 && finished==3);
    at=count=0;vp_hit_at=1000;add(AMOTION_EVENT_ACTION_DOWN,50,50);events[0].source=AINPUT_SOURCE_MOUSE;
    assert(vp_get_event(NULL,&event)==0 && finished==3);
    puts("Vulkan input: game/UI gesture ownership, drag capture, source filter, stale regions and delivery failure passed");
}
'''
ndk=Path(os.environ.get('NDK_HOME','/opt/homebrew/share/android-ndk'))
includes=next((ndk/'toolchains/llvm/prebuilt').glob('*/sysroot/usr/include'))
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'test.c';binary=Path(d)/'test';c.write_text(source)
    subprocess.run(['cc','-std=gnu11','-idirafter',str(includes),str(c),'-pthread','-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
