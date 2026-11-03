/* Owned-memory fixtures: no game process, adb or external data. */
#include <assert.h>
#include <stdio.h>
#include "../loot_value.h"
#define MEMORY_BASE UINT64_C(0x200000000)
#define MODULE_BASE UINT64_C(0x7000000000)
static unsigned char memory[1024 * 1024];
static size_t used;
static uint64_t main_vm;
static LootValueReader reader;
static uint64_t alloc(size_t size) { uint64_t p = MEMORY_BASE + used; used = (used + size + 7) & ~(size_t)7; assert(used < sizeof(memory)); return p; }
static void put(uint64_t p, const void *data, size_t n) { assert(p >= MEMORY_BASE && p + n <= MEMORY_BASE + sizeof(memory)); memcpy(memory + p - MEMORY_BASE, data, n); }
static void ptr(uint64_t p, uint64_t value) { put(p, &value, 8); }
static void integer(uint64_t p, int32_t value) { put(p, &value, 4); }
static int read_fixture(void *unused, uint64_t p, void *out, size_t n) {
    (void)unused;
    if (p == MODULE_BASE + 0x1fc9cd70 && n == 8) { memcpy(out, &main_vm, 8); return 1; }
    if (p < MEMORY_BASE || p - MEMORY_BASE > sizeof(memory) || n > sizeof(memory) - (size_t)(p - MEMORY_BASE)) return 0;
    memcpy(out, memory + p - MEMORY_BASE, n); return 1;
}
static uint64_t table(unsigned log_count) {
    uint64_t p = alloc(64), nodes = alloc((1U << log_count) * sizeof(LVNode));
    memory[p - MEMORY_BASE + 8] = 5; memory[p - MEMORY_BASE + 11] = (unsigned char)log_count;
    ptr(p + 24, nodes); return p;
}
static uint64_t string(const char *text) {
    size_t n = strlen(text); assert(n < 128); uint64_t p = alloc(24 + n + 1);
    memory[p - MEMORY_BASE + 11] = (unsigned char)n; put(p + 24, text, n); return p;
}
static uint64_t nodes_of(uint64_t t) { uint64_t p; assert(read_fixture(NULL, t + 24, &p, 8)); return p; }
static void field(uint64_t t, unsigned slot, const char *name, int tag, uint64_t value) {
    LVNode n = {.value = {.value = value, .tag = tag}, .key = string(name), .key_tag = 68}; put(nodes_of(t) + slot * sizeof(n), &n, sizeof(n));
}
static void numeric_key(uint64_t t, unsigned slot, uint64_t key, int tag, uint64_t value) {
    LVNode n = {.value = {.value = value, .tag = tag}, .key = key, .key_tag = 19}; put(nodes_of(t) + slot * sizeof(n), &n, sizeof(n));
}
static void item(uint64_t p, uint32_t category, uint32_t sequence, int32_t count) {
    uint32_t pair[2] = {category, sequence}; put(p + 0x10, pair, sizeof(pair)); integer(p + 0x38, count);
}
int main(void) {
    uint64_t vm = alloc(64), g = alloc(128), registry = table(0), roots = alloc(32);
    main_vm = vm; memory[vm - MEMORY_BASE + 8] = 8; ptr(vm + 24, g);
    LVValue reg = {.value = registry, .tag = 69}; put(g + 0x40, &reg, sizeof(reg)); ptr(registry + 16, roots); integer(registry + 12, 2);
    uint64_t globals = table(0), server = table(0), shop = table(1), data = table(0), normal = table(0), prices = table(3);
    LVValue root[2] = {{.value = vm, .tag = 72}, {.value = globals, .tag = 69}}; put(roots, root, sizeof(root));
    field(globals, 0, "Server", 69, server); field(server, 0, "ShopServer", 69, shop);
    field(shop, 0, "_data", 69, data); field(shop, 1, "_shopType", 68, string("Normal"));
    field(data, 0, "Normal", 69, normal); field(normal, 0, "_mapItemId2DynamicGuidePrice", 69, prices);
    numeric_key(prices, 0, 15080050111ULL, 19, 2798);
    numeric_key(prices, 1, 15090010026ULL, 19, 25000);
    numeric_key(prices, 2, 15080050056ULL, 19, UINT64_MAX); /* negative price */
    numeric_key(prices, 3, 11050003003ULL, 19, 90000); /* unadjusted equipment quote */
    double fractional = 25000.5; uint64_t bits; memcpy(&bits, &fractional, 8);
    numeric_key(prices, 4, 15080050057ULL, 3, bits);
    numeric_key(prices, 5, 15050010001ULL, 19, 100000); /* key before durability adjustment */
    reader.read = read_fixture; reader.base = MODULE_BASE;
    lv_begin_frame(&reader);
    lv_refresh(&reader, 10000); assert(reader.node_count == 8);
    int64_t value = 0;
    assert(lv_unit_price(&reader, 15080050111ULL, &value) && value == 2798);
    assert(!lv_unit_price(&reader, 15080050056ULL, &value)); /* negative price */
    assert(!lv_unit_price(&reader, 99999999999ULL, &value)); /* no invented fallback */
    assert(!lv_unit_price(&reader, 15080050057ULL, &value)); /* fractional price */
    uint64_t equipment = alloc(0x3000); item(equipment + 0x1240, 1105000, 3003, 1);
    LootValue gear = lv_actor(&reader, equipment, 1, 10100);
    assert(gear.contents_known && !gear.value_known && gear.item_id == 11050003003ULL);
    uint64_t key = alloc(0x3000); item(key + 0x1240, 1505001, 1, 1);
    LootValue key_result = lv_actor(&reader, key, 1, 10100);
    assert(key_result.contents_known && !key_result.value_known && key_result.item_id == 15050010001ULL);
    uint64_t loose = alloc(0x3000); item(loose + 0x1240, 1508005, 111, 99);
    LootValue result = lv_actor(&reader, loose, 1, 10100);
    assert(result.value_known && result.unit_value == 2798 && result.item_id == 15080050111ULL);
    assert(!result.container); /* stack count must not multiply the unit price */
    uint64_t box = alloc(0x3000), contents = alloc(0x1000);
    ptr(box + 0x2040, contents); integer(box + 0x2048, 0); integer(box + 0x204c, 2);
    result = lv_actor(&reader, box, 2, 10100);
    assert(result.container && !result.contents_known && !result.value_known);
    item(contents, 1508005, 111, 1); item(contents + 0x800, 1509001, 26, 1);
    integer(box + 0x2048, 2);
    result = lv_actor(&reader, box, 2, 10700);
    assert(result.contents_known && result.value_known && result.unit_value == 25000);
    assert(result.item_id == 15090010026ULL); /* highest individual item, not sum */
    integer(box + 0x2048, 1);
    result = lv_actor(&reader, box, 2, 11300); assert(result.unit_value == 2798);
    integer(box + 0x2048, 0); memory[box - MEMORY_BASE + 0x2428] = 1;
    result = lv_actor(&reader, box, 2, 11900); assert(result.contents_known && !result.value_known);
    integer(box + 0x2048, 129);
    result = lv_actor(&reader, box, 2, 12500); assert(!result.value_known);
    main_vm = 0; result = lv_actor(&reader, loose, 1, 14000); assert(!result.value_known);
    assert(!reader.node_count); /* disconnect cannot retain old prices */
    main_vm = vm; reader.read_budget = 0;
    result = lv_actor(&reader, loose, 1, 16000); assert(!result.value_known);
    lv_begin_frame(&reader);
    result = lv_actor(&reader, loose, 1, 18000); assert(result.value_known && result.unit_value == 2798);
    field(shop, 1, "_shopType", 68, string("Arena"));
    reader.map = 0; result = lv_actor(&reader, loose, 1, 20000);
    assert(!result.value_known && !reader.node_count); /* never use Normal prices in Arena */
    puts("loot value fixtures passed");
    return 0;
}
