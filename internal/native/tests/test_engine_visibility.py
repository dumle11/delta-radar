"""Run the production query bridge against a synthetic engine and dispatcher."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
source = (root / "internal/native/engine_visibility.h").read_text()
helpers = source[source.index("#define EV_MAX "):source.index("/* Worker-only lookup.")]
execute = source[source.index("static void ev_execute(void)"):source.index("static void ev_process_event(")]
program = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
#include "self-memory.h"
#define OFF_GUOBJECTARRAY_FROM_BASE 0x206aef48
static uint64_t ue4_base;
''' + helpers + '\nstatic void ea_frame(EVFrame *frame) { (void)frame; }\n' + execute + r'''
static unsigned char object[7][0x500], array[0x28], items[7][0x18];
static unsigned char instance[0x40], local[0x38];
static uint64_t chunks[1], locals[1];
static int calls, blocked_mask, no_dispatch;
static void put64(void *p, uint64_t value) { memcpy(p, &value, 8); }
static void put32(void *p, uint32_t value) { memcpy(p, &value, 4); }
static void dispatch(void *receiver, void *function, void *parameters) {
    assert(receiver == object[5] && function == object[6]);
    EVTraceParams *p = parameters;
    assert(p->world == (uintptr_t)object[0]);
    assert(p->channel == 3 && p->complex == 1);
    assert(p->ignore.count == 2 && p->ignore.capacity == 2);
    uint64_t *ignored = (void *)(uintptr_t)p->ignore.data;
    assert(ignored[0] == (uintptr_t)object[2] && ignored[1] == (uintptr_t)object[3]);
    assert(p->start[0] == 1000 && p->end[0] == 2000);
    int ray = calls++ % 5;
    if (!no_dispatch) p->result = (blocked_mask >> ray) & 1;
}
static void enqueue(void) {
    ev_request.sequence++;
    ev_request.at_ms = ev_now();
    ev_request.count = 1;
    ev_request.jobs[0].visible = -1;
}
int main(void) {
    ue4_base = (uintptr_t)array - OFF_GUOBJECTARRAY_FROM_BASE;
    chunks[0] = (uintptr_t)items;
    put32(array + 0x14, 7); put64(array + 0x20, (uintptr_t)chunks);
    for (int i = 0; i < 7; i++) {
        put64(object[i] + 8, 0x9990); put64(object[i] + 0x1c, i + 1);
        put32(object[i] + 0x24, i);
        put64(items[i], (uintptr_t)object[i]); put32(items[i] + 0x10, 100 + i);
    }
    put64(object[0] + 0x190, (uintptr_t)instance);
    put64(instance + 0x38, (uintptr_t)locals); locals[0] = (uintptr_t)local;
    put64(local + 0x30, (uintptr_t)object[1]);
    put64(object[1] + 0x3f0, (uintptr_t)object[2]);
    assert(ev_object((uintptr_t)object[0], &ev_request.world));
    assert(ev_object((uintptr_t)object[1], &ev_request.controller));
    assert(ev_object((uintptr_t)object[2], &ev_request.pawn));
    assert(ev_object((uintptr_t)object[3], &ev_request.jobs[0].actor));
    assert(ev_object((uintptr_t)object[5], &ev_receiver));
    assert(ev_object((uintptr_t)object[6], &ev_function));
    ev_request.camera[0] = 1000;
    for (int i = 0; i < 5; i++) ev_request.jobs[0].targets[i][0] = 2000;
    ev_dispatch = dispatch;
    blocked_mask = 31; enqueue(); ev_execute();
    assert(calls == 5 && ev_result.jobs[0].visible == 0 && ev_result.jobs[0].blocked == 31);
    ev_execute(); assert(calls == 5); /* consume a request only once */
    blocked_mask = 27; enqueue(); ev_execute();
    assert(calls == 10 && ev_result.jobs[0].visible == 1 && ev_result.jobs[0].blocked == 27);
    blocked_mask = 0; enqueue(); ev_execute();
    assert(calls == 15 && ev_result.jobs[0].visible == 1);
    no_dispatch = 1; enqueue(); ev_execute();
    assert(calls == 16 && ev_result.jobs[0].visible == -1);
    no_dispatch = 0;
    uint64_t previous = ev_result.sequence;
    enqueue(); ev_request.at_ms -= 1000; ev_execute();
    assert(calls == 16 && ev_result.sequence == previous);
    enqueue(); ev_request.at_ms += 1000; ev_execute();
    assert(calls == 16 && ev_result.sequence == previous);
    enqueue(); put64(object[1] + 0x3f0, 0); ev_execute();
    assert(calls == 16 && ev_result.sequence == previous);
    put64(object[1] + 0x3f0, (uintptr_t)object[2]);
    enqueue(); put32(items[3] + 0x10, 500); ev_execute();
    assert(calls == 16 && ev_result.jobs[0].visible == -1); /* recycled actor */
    put32(items[3] + 0x10, 103);
    enqueue(); put32(items[3] + 8, 0x20000000); ev_execute();
    assert(calls == 16 && ev_result.jobs[0].visible == -1); /* pending kill */
    put32(items[3] + 8, 0);
    enqueue(); pthread_mutex_lock(&ev_mutex); ev_execute(); pthread_mutex_unlock(&ev_mutex);
    assert(calls == 16); /* host callback never waits for worker */
    ev_execute(); assert(calls == 21);
    assert(ev_frame_fresh(&ev_result, &ev_request, ev_now()));
    assert(!ev_frame_fresh(&ev_result, &ev_request, ev_result.at_ms + 101));
    assert(!ev_frame_fresh(&ev_result, &ev_request, ev_result.at_ms - 1));
    ev_request.camera[0] += 26;
    assert(!ev_frame_fresh(&ev_result, &ev_request, ev_now()));
    ev_request.camera[0] -= 26;
    ev_request.camera[0] = NAN;
    assert(!ev_frame_fresh(&ev_result, &ev_request, ev_now()));
    ev_request.camera[0] = 1000;
    ev_request.world.address++;
    assert(!ev_frame_fresh(&ev_result, &ev_request, ev_now()));
    ev_request.world.address--;
    ev_request.pawn.address++;
    assert(!ev_frame_fresh(&ev_result, &ev_request, ev_now()));
    EVObject ref; assert(!ev_object(1, &ref));
    self_memory_close(&ev_reader);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    cfile = Path(directory) / "engine_visibility.c"
    executable = Path(directory) / "engine_visibility"
    cfile.write_text(program)
    subprocess.run(["cc", "-std=c11", "-O2", "-pthread", "-I", str(root / "external/scanner"),
                    str(cfile), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: trace ABI, channel/units/ignore list, cover, partial cover, failed dispatch, age, possession, lifetime, nonblocking publication")
