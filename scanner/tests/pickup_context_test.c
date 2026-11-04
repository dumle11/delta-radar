/* Owned-memory tests for the read-only Lua inventory copy. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include "../loot_value.h"
static LootValueReader loot_values;
static uint64_t ue4_base;
#include "../tools/pickup_context.h"
#include "../tools/pickup_ui_link.h"
#define BASE UINT64_C(0x200000000)
static unsigned char bytes[2 * 1024 * 1024];
static size_t used;
static uint64_t mutate_address;
static unsigned mutate_reads;
static unsigned mutate_byte_offset;
static uint64_t main_vm;
static uint64_t alloc(size_t n) {
    uint64_t p = BASE + used; used = (used + n + 7) & ~(size_t)7;
    assert(used < sizeof(bytes)); return p;
}
static void put(uint64_t p, const void *src, size_t n) {
    assert(p >= BASE && p - BASE + n <= sizeof(bytes)); memcpy(bytes + p - BASE, src, n);
}
static int fixture_read(void *unused, uint64_t p, void *out, size_t n) {
    (void)unused;
    if (p == ue4_base + 0x1fc9cd70 && n == 8) { memcpy(out, &main_vm, 8); return 1; }
    if (p < BASE || p - BASE > sizeof(bytes) || n > sizeof(bytes) - (p - BASE)) return 0;
    if (p == mutate_address && ++mutate_reads == 2) bytes[p - BASE + mutate_byte_offset] ^= 1;
    memcpy(out, bytes + p - BASE, n); return 1;
}
static void ptr(uint64_t p, uint64_t v) { put(p, &v, 8); }
static uint64_t table(unsigned log, unsigned arrays) {
    uint64_t p = alloc(64), nodes = alloc((1U << log) * sizeof(LVNode));
    bytes[p - BASE + 8] = 5; bytes[p - BASE + 11] = (unsigned char)log;
    uint32_t n = arrays; put(p + 12, &n, 4); ptr(p + 24, nodes);
    if (arrays) ptr(p + 16, alloc(arrays * sizeof(LVValue)));
    return p;
}
static uint64_t getptr(uint64_t p) { uint64_t v; assert(fixture_read(NULL, p, &v, 8)); return v; }
static void reset_budget(void) { loot_values.read_budget = 32768; loot_values.budget_enabled = 1; }
static uint64_t string(const char *s) {
    size_t len = strlen(s); assert(len < 64); uint64_t p = alloc(24 + len + 1);
    bytes[p - BASE + 8] = 4; bytes[p - BASE + 11] = (unsigned char)len;
    put(p + 24, s, len); return p;
}
static void field(uint64_t t, unsigned slot, const char *name, int tag, uint64_t value) {
    LVNode n = {.value = {.value = value, .tag = tag}, .key = string(name), .key_tag = 68};
    put(getptr(t + 24) + slot * sizeof(n), &n, sizeof(n));
}
static void number_key(uint64_t t, unsigned slot, uint64_t key, int tag, uint64_t value) {
    LVNode n = {.value = {.value = value, .tag = tag}, .key = key, .key_tag = 19};
    put(getptr(t + 24) + slot * sizeof(n), &n, sizeof(n));
}
static void context_output(char *out, size_t capacity) {
    FILE *scratch = tmpfile(); assert(scratch);
    fflush(stdout); int saved = dup(STDOUT_FILENO); assert(saved >= 0);
    assert(dup2(fileno(scratch), STDOUT_FILENO) >= 0);
    assert(pickup_context()); fflush(stdout);
    assert(dup2(saved, STDOUT_FILENO) >= 0); close(saved); rewind(scratch);
    size_t n = fread(out, 1, capacity - 1, scratch); assert(n < capacity - 1);
    out[n] = 0; fclose(scratch);
}
static void full_context_fixture(void) {
    uint64_t vm = alloc(64), g = alloc(128), reg = table(0, 0), roots = alloc(32);
    main_vm = vm; ue4_base = UINT64_C(0x7000000000);
    bytes[vm - BASE + 8] = 8; ptr(vm + 24, g);
    LVValue rv = {.value = reg, .tag = 69}; put(g + 0x40, &rv, sizeof(rv));
    uint64_t globals = table(2, 0), server = table(1, 0), inv = table(1, 0), enums = table(2, 0);
    LVValue root_values[2] = {{.value = vm, .tag = 72}, {.value = globals, .tag = 69}};
    put(roots, root_values, sizeof(root_values)); ptr(reg + 16, roots);
    field(globals, 0, "Server", 69, server); field(globals, 1, "ESlotGroup", 69, enums);
    uint64_t pickup_enums = table(0, 0); field(pickup_enums, 0, "SceneBox", 19, 1);
    field(globals, 3, "EGamePickupType", 69, pickup_enums);
    field(server, 0, "InventoryServer", 69, inv);
    uint64_t looting = table(0, 0); field(server, 1, "LootingServer", 69, looting);
    field(enums, 0, "Player", 19, 1); field(enums, 1, "Nearby", 19, 3); field(enums, 2, "DeadBody", 19, 2);
    uint64_t types = table(3, 0); field(globals, 2, "ESlotType", 69, types);
    const char *names[] = {"BagContainer", "ChestHangingContainer", "NearbyContainer", "DeadbodyLootBox", "SafeBoxContainer", "Pocket"};
    for (unsigned i = 0; i < 6; i++) field(types, i, names[i], 19, 300 + i);
    uint64_t maps = table(2, 0), slots = table(2, 0), nearby = table(0, 0);
    field(inv, 0, "_mapGid2Items", 69, maps); field(inv, 1, "_allItemSlots", 69, slots);
    for (unsigned i = 1; i <= 3; i++) {
        number_key(maps, i - 1, i, 69, i == 3 ? nearby : table(0, 0));
        number_key(slots, i - 1, i, 69, table(0, 0));
    }
    uint64_t data = table(4, 0), in_slot = table(1, 0), gid = UINT64_C(0xf123456789abcde0);
    number_key(nearby, 0, gid, 69, data);
    field(data, 0, "gid", 19, gid); field(data, 1, "id", 19, UINT64_C(15080050015));
    field(data, 2, "num", 19, 1); field(data, 3, "width", 19, 1); field(data, 4, "length", 19, 1);
    field(data, 5, "maxStackCount", 19, 1); field(data, 6, "itemMainType", 19, 15);
    field(data, 7, "itemSubType", 19, 8); field(data, 8, "searchDoneStamp", 19, UINT64_MAX);
    field(data, 9, "InSlot", 69, in_slot); field(in_slot, 0, "SlotType", 19, 302);
    field(in_slot, 1, "_slotGroupId", 19, 3);
    loot_values.node_count = 1;
    loot_values.nodes[0] = (LVNode){.value = {.value = 62800, .tag = 19}, .key = UINT64_C(15080050015), .key_tag = 19};
    char out[8192]; context_output(out, sizeof(out));
    assert(strstr(out, "\"gid\":\"f123456789abcde0\""));
    assert(strstr(out, "\"unitValueKnown\":true,\"unitValue\":62800"));
    assert(strstr(out, "\"complete\":true,\"actionReady\":false"));
    assert(strstr(out, "\"record\":\"selector\",\"present\":false"));
    uint64_t selected = table(1, 0), interactors = table(0, 1);
    LVValue interactor = {.value = UINT64_C(0x300001000), .tag = 71};
    put(getptr(interactors + 16), &interactor, sizeof(interactor));
    field(selected, 0, "type", 19, 1); field(selected, 1, "interactors", 69, interactors);
    field(looting, 0, "_currentSelectorData", 69, selected); context_output(out, sizeof(out));
    assert(strstr(out, "\"record\":\"selector\",\"present\":true,\"sceneBoxType\":1"));
    assert(strstr(out, "\"type\":1,\"interactorCount\":1,\"interactorsReadable\":true"));
    field(looting, 0, "_currentSelectorData", 0, 0);
    /* Map key/GID disagreement is rejected instead of misidentifying an item. */
    field(data, 0, "gid", 19, gid + 1); context_output(out, sizeof(out));
    assert(!strstr(out, "\"record\":\"item\""));
    assert(strstr(out, "\"complete\":false,\"actionReady\":false"));
    /* A lobby stash can add 100+ Player slots. Read only the two enum-verified
     * destinations, without treating unrelated stash slots as carried space. */
    field(data, 0, "gid", 19, gid);
    uint64_t player_slots = table(7, 0), small_empty = table(0, 0);
    number_key(slots, 0, 1, 69, player_slots);
    for (unsigned i = 0; i < 107; i++) {
        unsigned kind = i < 2 ? 300 + i : 1000 + i;
        uint64_t s = table(4, 0);
        number_key(player_slots, i, kind, 69, s);
        field(s, 0, "SlotType", 19, kind); field(s, 1, "Width", 19, 3); field(s, 2, "Length", 19, 2);
        field(s, 3, "_slotGroupId", 19, 1); field(s, 4, "_totalCapacity", 19, 6);
        field(s, 5, "serverUpdateCount", 19, 1);
        const char *map_names[] = {"_itemsPosition", "_itemsAbsLocX", "_itemsAbsLocY", "_spaceBatch", "_containItems"};
        for (unsigned j = 0; j < 5; j++) field(s, 6 + j, map_names[j], 69, small_empty);
    }
    char filtered[16384]; context_output(filtered, sizeof(filtered));
    assert(strstr(filtered, "\"entries\":107,\"backpackRigOnly\":true"));
    assert(strstr(filtered, "\"group\":\"Player\",\"SlotType\":300"));
    assert(strstr(filtered, "\"group\":\"Player\",\"SlotType\":301"));
    assert(!strstr(filtered, "\"SlotType\":1002"));
    assert(strstr(filtered, "\"complete\":true,\"actionReady\":false"));
    /* Even a readable oversized position map cannot generate unbounded output. */
    field(data, 0, "gid", 19, gid);
    uint64_t nearby_slots = table(0, 0), position_map = table(13, 0), empty_map = table(0, 0);
    number_key(slots, 2, 3, 69, nearby_slots); number_key(nearby_slots, 0, 302, 69, in_slot);
    uint64_t full_slot = table(4, 0); number_key(nearby_slots, 0, 302, 69, full_slot);
    field(full_slot, 0, "SlotType", 19, 302); field(full_slot, 1, "Width", 19, 16);
    field(full_slot, 2, "Length", 19, 16); field(full_slot, 3, "_slotGroupId", 19, 3);
    field(full_slot, 4, "_totalCapacity", 19, 256); field(full_slot, 5, "serverUpdateCount", 19, 1);
    field(full_slot, 6, "_itemsPosition", 69, position_map);
    const char *maps_more[] = {"_itemsAbsLocX", "_itemsAbsLocY", "_spaceBatch", "_containItems"};
    for (unsigned i = 0; i < 4; i++) field(full_slot, 7 + i, maps_more[i], 69, empty_map);
    for (unsigned i = 0; i < 8192; i++) number_key(position_map, i, i + 1, 19, i);
    char *large_output = malloc(2 * 1024 * 1024); assert(large_output);
    context_output(large_output, 2 * 1024 * 1024);
    assert(strstr(large_output, "\"dataRecords\":4096"));
    assert(strstr(large_output, "\"complete\":false,\"actionReady\":false"));
    unsigned lines = 0; for (char *p = large_output; *p; p++) if (*p == '\n') lines++;
    assert(lines == PC_MAX_RECORDS + 1); free(large_output);
}
static void ui_link_fixture(void) {
    reset_budget();
    uint64_t item = table(3, 0), view = table(3, 0), slot = table(1, 0), wrapper = alloc(64), object = alloc(64);
    uint64_t gid = UINT64_C(0xf123456789abcde0);
    field(item, 0, "gid", 19, gid); field(item, 1, "id", 19, UINT64_C(15080050238));
    field(item, 2, "num", 19, 1); field(item, 3, "width", 19, 2); field(item, 4, "length", 19, 1);
    field(item, 5, "InSlot", 69, slot); field(slot, 0, "_slotGroupId", 19, 3); field(slot, 1, "SlotType", 19, 302);
    field(view, 0, "item", 69, item); field(view, 1, "__cppinst", 71, wrapper);
    field(view, 2, "_has_destroy_", 1, 0); field(view, 3, "_is_enable_", 1, 1);
    field(view, 4, "bIsShow", 1, 0); field(view, 5, "_bDraging", 1, 0); field(view, 6, "_viewSearchState", 19, 0);
    bytes[wrapper - BASE + 8] = 7; ptr(wrapper + 24, 24); ptr(wrapper + 56, object);
    PCEntry entry = {.key = item, .key_tag = 69, .value = {.value = view, .tag = 69}};
    PCContext ctx = {.complete = 1}; PUICell cell = {0};
    assert(pui_cell_link(&ctx, &entry, 3, 302, &cell) && ctx.complete);
    assert(cell.gid == gid && cell.catalog == UINT64_C(15080050238) && cell.widget == object);
    assert(cell.columns == 1 && cell.rows == 2 && cell.raw_show == 0 && cell.enabled == 1);
    /* False raw-show is retained, not guessed to mean invisible. */
    field(view, 0, "item", 69, item + 8); ctx = (PCContext){.complete = 1};
    assert(!pui_cell_link(&ctx, &entry, 3, 302, &cell) && !ctx.complete);
    field(view, 0, "item", 69, item); ctx = (PCContext){.complete = 1};
    assert(!pui_cell_link(&ctx, &entry, 1, 302, &cell));
    ctx = (PCContext){.complete = 1}; assert(!pui_cell_link(&ctx, &entry, 3, 108001, &cell));
    ptr(wrapper + 24, 32); ctx = (PCContext){.complete = 1};
    assert(!pui_cell_link(&ctx, &entry, 3, 302, &cell)); ptr(wrapper + 24, 24);
    field(view, 2, "_has_destroy_", 1, 1); ctx = (PCContext){.complete = 1};
    assert(!pui_cell_link(&ctx, &entry, 3, 302, &cell)); field(view, 2, "_has_destroy_", 1, 0);
    mutate_address = wrapper; mutate_reads = 0; mutate_byte_offset = 56; ctx = (PCContext){.complete = 1};
    assert(!pui_cell_link(&ctx, &entry, 3, 302, &cell)); mutate_address = 0; mutate_byte_offset = 0;
}
int main(void) {
    loot_values.read = fixture_read; reset_budget();
    PCContext ctx = {.complete = 1};
    uint64_t t = table(1, 2), a = getptr(t + 16), nodes = getptr(t + 24);
    LVValue av[2] = {{.value = 7, .tag = 19}, {0}}; put(a, av, sizeof(av));
    uint64_t high_gid = UINT64_C(0xf123456789abcde0);
    LVNode n = {.value = {.value = 42, .tag = 19}, .key = high_gid, .key_tag = 19};
    put(nodes, &n, sizeof(n));
    PCTable copied = pc_load(&ctx, t);
    assert(copied.valid && ctx.complete && copied.count == 2);
    assert(pc_numeric(&copied, 1).value == 7);
    int64_t signed_gid; memcpy(&signed_gid, &high_gid, 8);
    assert(pc_numeric(&copied, signed_gid).value == 42); pc_free(&copied);
    assert(pc_supported_collectible(15080050015ULL));
    assert(!pc_supported_collectible(15050010001ULL));
    assert(!pc_supported_collectible(11050003001ULL));
    int64_t integer;
    double f = 1.5; uint64_t bits; memcpy(&bits, &f, 8);
    assert(!pc_integer((LVValue){.value = bits, .tag = 3}, &integer));
    f = INFINITY; memcpy(&bits, &f, 8);
    assert(!pc_integer((LVValue){.value = bits, .tag = 3}, &integer));
    /* Same count/header, different payload must invalidate the copied table. */
    mutate_address = nodes; mutate_reads = 0; ctx = (PCContext){.complete = 1};
    copied = pc_load(&ctx, t); assert(!copied.valid && !ctx.complete); pc_free(&copied);
    mutate_address = 0;
    /* An oversized array and an unreadable table must not appear as empty. */
    uint32_t too_many = PC_MAX_ARRAY + 1; put(t + 12, &too_many, 4);
    ctx = (PCContext){.complete = 1}; copied = pc_load(&ctx, t);
    assert(!copied.valid && !ctx.complete); pc_free(&copied);
    ctx = (PCContext){.complete = 1}; copied = pc_load(&ctx, BASE + sizeof(bytes) + 1);
    assert(!copied.valid && !ctx.complete); pc_free(&copied);
    /* Budget exhaustion is incomplete, not empty/valid. */
    loot_values.read_budget = 0; ctx = (PCContext){.complete = 1}; copied = pc_load(&ctx, table(0, 0));
    assert(!copied.valid && !ctx.complete); pc_free(&copied);
    /* The captured globals have over 1,700 names. They must not be rejected by
     * a limit intended for the much smaller item lists. */
    reset_budget(); uint64_t big = table(11, 0);
    for (unsigned i = 0; i < 1725; i++) {
        char name[32]; snprintf(name, sizeof(name), "global_%u", i); field(big, i, name, 19, i);
    }
    ctx = (PCContext){.complete = 1}; copied = pc_load(&ctx, big);
    assert(copied.valid && ctx.complete && copied.count == 1725); pc_free(&copied);
    reset_budget(); full_context_fixture(); ui_link_fixture();
    puts("pickup context owned-memory tests passed");
    return 0;
}
