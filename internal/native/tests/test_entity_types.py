"""Verify production NPC/player-bot classification and team-independent routing."""
from pathlib import Path
import subprocess
import tempfile

native = Path(__file__).resolve().parents[1]
reader = (native.parents[1] / "external/scanner/memscan-legacy.c").read_text()
overlay = (native / "esp_overlay.h").read_text()
classification = reader[reader.index("static int class_is_world_npc("):reader.index("static void read_actor_identity(")]
mapping_start = overlay.index("static int _ov_entity_type(")
mapping = overlay[mapping_start:overlay.index("\n}", mapping_start) + 2]
defines = overlay[overlay.index("#define ET_ENEMY"):overlay.index("/* Feature flags matching O.java */")]
program = r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
static unsigned char heap[4096];
static uint64_t verified_uworld = 1;
#define OFF_UOBJECT_NAME 0x1c
typedef struct { char kind[16]; int team, is_player_bot; } Entity;
static int is_telemetry_ptr(uint64_t p) { return p >= (uint64_t)heap && p < (uint64_t)(heap + sizeof(heap)); }
static uint64_t read_ptr(uint64_t p) { uint64_t v = 0; memcpy(&v, (void *)p, 8); return v; }
static int32_t read_i32(uint64_t p) { int32_t v = 0; memcpy(&v, (void *)p, 4); return v; }
static int resolve_fname(int id, char *out, int size) {
    const char *names[] = {"", "DFMPlayerCharacter", "DFMAICharacter", "DFMRangeTargetCharacter", "BP_Player_C", "BP_NPC_C"};
    if (id < 0 || id >= 6 || !names[id][0]) return 0;
    strncpy(out, names[id], size - 1); return (int)strlen(out);
}
static void set_name(uint64_t cls, int id) { memcpy((void *)(cls + 0x1c), &id, 4); }
static void set_parent(uint64_t cls, uint64_t parent) { memcpy((void *)(cls + 0x40), &parent, 8); }
""" + defines + classification + mapping + r"""
int main(void) {
    uint64_t player = (uint64_t)heap + 256, ai = (uint64_t)heap + 512;
    uint64_t player_bp = (uint64_t)heap + 768, npc_bp = (uint64_t)heap + 1024;
    uint64_t range = (uint64_t)heap + 1280, retry = (uint64_t)heap + 1536;
    set_name(player,1); set_name(ai,2); set_name(player_bp,4); set_name(npc_bp,5); set_name(range,3);
    set_parent(player_bp,player); set_parent(npc_bp,ai);
    assert(!class_is_world_npc(player_bp));
    assert(class_is_world_npc(npc_bp) && class_is_world_npc(range));
    assert(!class_is_world_npc(retry)); set_name(retry,2);
    assert(class_is_world_npc(retry)); /* A temporary name failure is not cached. */
    /* Unreadable names on derived classes must not hide a known native base. */
    uint64_t native_ai=(uint64_t)heap+1792, base=(uint64_t)heap+2048;
    uint64_t derived=(uint64_t)heap+2304;
    int ai_index=0xFDC, base_index=0xF5F;
    memcpy((void *)(native_ai+0x24), &ai_index, 4);
    memcpy((void *)(base+0x24), &base_index, 4);
    set_parent(native_ai,base); set_parent(derived,native_ai);
    assert(class_is_world_npc(derived));
    verified_uworld++;
    ai_index=0x1C0D; memcpy((void *)(native_ai+0x24), &ai_index, 4);
    assert(class_is_world_npc(derived)); /* Range-target class with no names. */
    verified_uworld++;
    ai_index=0x1C5D; memcpy((void *)(native_ai+0x24), &ai_index, 4);
    assert(!class_is_world_npc(derived));
    assert(player_bot_from_state(1,0x8,class_is_world_npc(derived)));
    verified_uworld++;
    ai_index=0xFDC; memcpy((void *)(native_ai+0x24), &ai_index, 4);
    base_index=0x123; memcpy((void *)(base+0x24), &base_index, 4);
    assert(!class_is_world_npc(derived)); /* Pair mismatch does not classify. */
    assert(!player_bot_from_state(0,0x8,0));
    assert(!player_bot_from_state(1,0,0));
    assert(player_bot_from_state(1,0x8,class_is_world_npc(player_bp)));
    assert(!player_bot_from_state(1,0x8,class_is_world_npc(npc_bp)));
    Entity e = {.kind="p", .team=2};
    assert(_ov_entity_type(&e)==ET_ENEMY); e.team=1;
    assert(_ov_entity_type(&e)==ET_TEAMMATE);
    strcpy(e.kind,"a"); e.is_player_bot=1;
    assert(_ov_entity_type(&e)==ET_BOT_PLAYER); e.team=2;
    assert(_ov_entity_type(&e)==ET_BOT_PLAYER);
    e.is_player_bot=0;
    assert(_ov_entity_type(&e)==ET_NPC); e.team=1;
    assert(_ov_entity_type(&e)==ET_NPC);
    strcpy(e.kind,"l"); assert(_ov_entity_type(&e)==ET_LOOT);
    return 0;
}
"""
with tempfile.TemporaryDirectory() as directory:
    source = Path(directory) / "types.c"
    executable = Path(directory) / "types"
    source.write_text(program)
    subprocess.run(["cc", "-O2", str(source), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)
print("PASS: real players, NPC ancestry, simulated players, and team-independent AI categories")
