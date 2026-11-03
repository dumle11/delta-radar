/* Read-only pickup research snapshot. Never a command/transfer interface.
 * Included by loot_probe.c after memscan.c; Lua tables are copied with bounds
 * and reread to detect concurrent changes. A stable copy is not an atomic
 * inventory transaction or proof that a particular loot window is open. */
#ifndef DELTA_PICKUP_CONTEXT_H
#define DELTA_PICKUP_CONTEXT_H

#define PC_MAX_ENTRIES 8192U
#define PC_MAX_ARRAY 1024U
#define PC_MAX_ITEMS 256U
#define PC_MAX_SLOTS 64U
#define PC_MAX_RECORDS 4096U
typedef struct {
    LVValue value;
    uint64_t key;
    int key_tag;
    char name[64];
} PCEntry;
typedef struct { PCEntry *entries; unsigned count; int valid; } PCTable;
typedef struct { uint64_t address; unsigned char header[32]; } PCGuard;
typedef struct { PCGuard guards[4096]; unsigned count, records; int complete; } PCContext;

static int pc_emit(PCContext *ctx) {
    if (ctx->records >= PC_MAX_RECORDS) { ctx->complete = 0; return 0; }
    ctx->records++; return 1;
}

static int pc_integer(LVValue v, int64_t *out) {
    if (v.tag == 19) { memcpy(out, &v.value, 8); return 1; }
    if (v.tag == 3) {
        double d; memcpy(&d, &v.value, 8);
        if (isfinite(d) && fabs(d) <= 9007199254740991.0 && floor(d) == d) {
            *out = (int64_t)d; return 1;
        }
    }
    return 0;
}

static void pc_free(PCTable *t) { free(t->entries); memset(t, 0, sizeof(*t)); }

static PCTable pc_load(PCContext *ctx, uint64_t address) {
    PCTable t = {0}; unsigned char h[32], after[32];
    LVNode *nodes = NULL, *check_nodes = NULL;
    LVValue *array = NULL, *check_array = NULL;
    if (!lv_read(&loot_values, address, h, sizeof(h)) || h[8] != 5 || h[11] > 13) goto fail;
    uint32_t na; uint64_t ap, np; unsigned nn = 1U << h[11];
    memcpy(&na, h + 12, 4); memcpy(&ap, h + 16, 8); memcpy(&np, h + 24, 8);
    if (na > PC_MAX_ARRAY || (na && !lv_pointer(ap)) || !lv_pointer(np)) goto fail;
    size_t nb = nn * sizeof(LVNode), ab = na * sizeof(LVValue);
    nodes = malloc(nb); check_nodes = malloc(nb);
    array = malloc(ab ? ab : 1); check_array = malloc(ab ? ab : 1);
    t.entries = calloc(PC_MAX_ENTRIES, sizeof(PCEntry));
    if (!nodes || !check_nodes || !array || !check_array || !t.entries ||
        !lv_read(&loot_values, np, nodes, nb) || (ab && !lv_read(&loot_values, ap, array, ab))) goto fail;
    for (unsigned i = 0; i < na + nn; i++) {
        LVValue value = i < na ? array[i] : nodes[i - na].value;
        if (!value.tag) continue;
        if (t.count == PC_MAX_ENTRIES) goto fail;
        PCEntry *e = &t.entries[t.count++]; e->value = value;
        e->key_tag = i < na ? 19 : nodes[i - na].key_tag;
        e->key = i < na ? (uint64_t)i + 1 : nodes[i - na].key;
        if (e->key_tag == 68 || e->key_tag == 84) {
            /* Longer/unrelated names need not be decoded. Keep the value and
             * count nonetheless, so a limit is never silently treated as empty. */
            unsigned char sh[24]; uint64_t len = 0;
            if (!lv_read(&loot_values, e->key, sh, sizeof(sh)) ||
                sh[8] != (e->key_tag == 68 ? 4 : 20)) goto fail;
            if (e->key_tag == 68) len = sh[11]; else memcpy(&len, sh + 16, 8);
            if (len > 0 && len < sizeof(e->name)) {
                if (!lv_read(&loot_values, e->key + 24, e->name, (size_t)len)) goto fail;
                for (size_t j = 0; j < len; j++) {
                    if ((unsigned char)e->name[j] < 32 || (unsigned char)e->name[j] > 126) {
                        /* Unrelated localized keys cannot match the ASCII
                         * schema fields, but are still included in the count. */
                        e->name[0] = 0; break;
                    }
                }
            }
        }
    }
    /* Check copied payload, not just count/capacity: a pickup can replace a
     * value without resizing the table. GC link/mark bytes are excluded. */
    if (!lv_read(&loot_values, address, after, sizeof(after)) ||
        memcmp(h + 10, after + 10, 22) ||
        !lv_read(&loot_values, np, check_nodes, nb) || memcmp(nodes, check_nodes, nb) ||
        (ab && (!lv_read(&loot_values, ap, check_array, ab) || memcmp(array, check_array, ab)))) goto fail;
    if (ctx->count == sizeof(ctx->guards) / sizeof(ctx->guards[0])) goto fail;
    ctx->guards[ctx->count].address = address;
    memcpy(ctx->guards[ctx->count++].header, h, sizeof(h));
    t.valid = 1;
    free(nodes); free(check_nodes); free(array); free(check_array); return t;
fail:
    free(nodes); free(check_nodes); free(array); free(check_array); pc_free(&t);
    ctx->complete = 0; return t;
}

static LVValue pc_field(const PCTable *t, const char *name) {
    for (unsigned i = 0; i < t->count; i++)
        if (!strcmp(t->entries[i].name, name)) return t->entries[i].value;
    return (LVValue){0};
}
static LVValue pc_numeric(const PCTable *t, int64_t key) {
    for (unsigned i = 0; i < t->count; i++) {
        int64_t candidate;
        if (pc_integer((LVValue){.value=t->entries[i].key, .tag=t->entries[i].key_tag}, &candidate) && candidate == key)
            return t->entries[i].value;
    }
    return (LVValue){0};
}
static uint64_t pc_table_value(PCContext *ctx, const PCTable *t, const char *name) {
    LVValue v = pc_field(t, name);
    if (v.tag != 69) { ctx->complete = 0; return 0; }
    return v.value;
}
static uint64_t pc_child(PCContext *ctx, uint64_t table, const char *key) {
    PCTable t = pc_load(ctx, table); uint64_t p = pc_table_value(ctx, &t, key); pc_free(&t);
    return p;
}
static void pc_print_number(const PCTable *t, const char *name) {
    int64_t n;
    printf(",\"%s\":", name);
    if (pc_integer(pc_field(t, name), &n)) printf("%ld", n); else printf("null");
}
static int pc_supported_collectible(uint64_t id) {
    unsigned subtype = (unsigned)((id / 10000 / 1000) % 100);
    return id / 10000 / 100000 == 15 && subtype >= 1 && subtype <= 9 && subtype != 5;
}

static void pc_items(PCContext *ctx, uint64_t address, const char *group) {
    PCTable map = pc_load(ctx, address);
    if (!pc_emit(ctx)) { pc_free(&map); return; }
    printf("{\"record\":\"itemMap\",\"group\":\"%s\",\"readable\":%s,\"entries\":%u}\n",
           group, map.valid ? "true" : "false", map.count);
    if (map.count > PC_MAX_ITEMS) { ctx->complete = 0; pc_free(&map); return; }
    for (unsigned i = 0; i < map.count; i++) {
        PCEntry *e = &map.entries[i];
        if (e->key_tag != 19 || e->value.tag != 69) { ctx->complete = 0; continue; }
        PCTable item = pc_load(ctx, e->value.value); LVValue gid = pc_field(&item, "gid");
        if (!item.valid || gid.tag != 19 || !gid.value || gid.value != e->key) {
            ctx->complete = 0; pc_free(&item); continue;
        }
        if (!pc_emit(ctx)) { pc_free(&item); break; }
        int64_t id = 0, price = 0;
        int priced = pc_integer(pc_field(&item, "id"), &id) && id > 0 &&
            pc_supported_collectible((uint64_t)id) && lv_unit_price(&loot_values, (uint64_t)id, &price);
        printf("{\"record\":\"item\",\"group\":\"%s\",\"gid\":\"%016lx\"", group, gid.value);
        const char *fields[] = {"id", "num", "width", "length", "maxStackCount", "itemMainType", "itemSubType", "searchDoneStamp"};
        for (unsigned j = 0; j < sizeof(fields)/sizeof(fields[0]); j++) pc_print_number(&item, fields[j]);
        printf(",\"unitValueKnown\":%s,\"unitValue\":%ld,\"valueSource\":\"%s\"",
               priced ? "true" : "false", priced ? price : 0, priced ? LV_PRICE_SOURCE : "");
        LVValue slot = pc_field(&item, "InSlot");
        if (slot.tag == 69) {
            PCTable s = pc_load(ctx, slot.value);
            printf(",\"inSlot\":{");
            int64_t n; LVValue st = pc_field(&s, "SlotType");
            printf("\"SlotType\":"); if (pc_integer(st, &n)) printf("%ld", n); else printf("null");
            pc_print_number(&s, "_slotGroupId"); printf("}"); pc_free(&s);
        }
        puts("}"); pc_free(&item);
    }
    pc_free(&map);
}

static void pc_slots(PCContext *ctx, uint64_t address, const char *group, int64_t bag, int64_t rig) {
    PCTable slots = pc_load(ctx, address);
    int player = !strcmp(group, "Player"); unsigned selected = 0;
    if (!pc_emit(ctx)) { pc_free(&slots); return; }
    printf("{\"record\":\"slotSet\",\"group\":\"%s\",\"readable\":%s,\"entries\":%u,\"backpackRigOnly\":%s}\n",
           group, slots.valid ? "true" : "false", slots.count, player ? "true" : "false");
    for (unsigned i = 0; i < slots.count; i++) {
        PCEntry *entry = &slots.entries[i];
        if (entry->key_tag != 19 || entry->value.tag != 69) { ctx->complete = 0; continue; }
        /* Player also holds the entire lobby stash. Its slot count can exceed
         * 64 even though only these two verified enum slots are destinations. */
        if (player && entry->key != (uint64_t)bag && entry->key != (uint64_t)rig) continue;
        if (++selected > PC_MAX_SLOTS) { ctx->complete = 0; break; }
        PCTable slot = pc_load(ctx, entry->value.value);
        int64_t kind = 0;
        if (!slot.valid || !pc_integer(pc_field(&slot, "SlotType"), &kind) ||
            (uint64_t)kind != entry->key) { ctx->complete = 0; pc_free(&slot); continue; }
        if (!pc_emit(ctx)) { pc_free(&slot); break; }
        printf("{\"record\":\"slot\",\"group\":\"%s\",\"SlotType\":%ld", group, kind);
        const char *fields[] = {"Width", "Length", "_slotGroupId", "_totalCapacity", "serverUpdateCount"};
        for (unsigned j = 0; j < sizeof(fields)/sizeof(fields[0]); j++) pc_print_number(&slot, fields[j]);
        puts("}");
        /* Positions/space batches are deliberately opaque until their actual
         * shape and coordinate convention have been verified in a live window. */
        const char *maps[] = {"_itemsPosition", "_itemsAbsLocX", "_itemsAbsLocY", "_spaceBatch", "_containItems"};
        for (unsigned m = 0; m < sizeof(maps)/sizeof(maps[0]); m++) {
            LVValue child = pc_field(&slot, maps[m]);
            if (child.tag != 69) { ctx->complete = 0; continue; }
            PCTable t = pc_load(ctx, child.value);
            if (!pc_emit(ctx)) { pc_free(&t); break; }
            printf("{\"record\":\"slotMap\",\"group\":\"%s\",\"SlotType\":%ld,\"map\":\"%s\",\"readable\":%s,\"entries\":%u}\n",
                   group, kind, maps[m], t.valid ? "true" : "false", t.count);
            for (unsigned j = 0; j < t.count; j++) {
                if (!pc_emit(ctx)) break;
                PCEntry *e = &t.entries[j]; int64_t n;
                printf("{\"record\":\"slotEntry\",\"group\":\"%s\",\"SlotType\":%ld,\"map\":\"%s\",\"keyTag\":%d,\"keyBits\":\"%016lx\",\"valueTag\":%d",
                       group, kind, maps[m], e->key_tag, e->key, e->value.tag);
                if (pc_integer(e->value, &n)) printf(",\"integer\":%ld", n);
                else if (e->value.tag == 3) {
                    double d; memcpy(&d, &e->value.value, 8);
                    if (isfinite(d)) printf(",\"number\":%.17g", d);
                }
                /* Do not emit arbitrary pointer values from child entries. */
                puts("}");
            }
            pc_free(&t);
        }
        pc_free(&slot);
    }
    pc_free(&slots);
}

static int pickup_context(void) {
    PCContext ctx = {.complete = 1};
    /* One-shot research, not a per-frame reader. Lobby Player includes cached
     * stash items: 135 of 255 items alone consumed the original 32K budget. */
    loot_values.read_budget = 131072;
    uint64_t vm = lv_ptr(&loot_values, ue4_base + 0x1fc9cd70);
    unsigned char type = 0; LVValue reg, roots[2];
    uint64_t g = lv_ptr(&loot_values, vm + 24);
    if (!lv_read(&loot_values, vm + 8, &type, 1) || type != 8 ||
        !lv_read(&loot_values, g + 0x40, &reg, sizeof(reg)) || reg.tag != 69 ||
        !lv_read(&loot_values, lv_ptr(&loot_values, reg.value + 16), roots, sizeof(roots)) ||
        roots[0].tag != 72 || roots[0].value != vm || roots[1].tag != 69) return 0;
    PCTable globals = pc_load(&ctx, roots[1].value);
    uint64_t server = pc_table_value(&ctx, &globals, "Server");
    uint64_t groups = pc_table_value(&ctx, &globals, "ESlotGroup");
    uint64_t slot_types = pc_table_value(&ctx, &globals, "ESlotType");
    pc_free(&globals);
    uint64_t inventory = pc_child(&ctx, server, "InventoryServer");
    uint64_t maps = pc_child(&ctx, inventory, "_mapGid2Items");
    uint64_t slots = pc_child(&ctx, inventory, "_allItemSlots");
    PCTable enums = pc_load(&ctx, groups), item_groups = pc_load(&ctx, maps), slot_groups = pc_load(&ctx, slots);
    /* Retain only named slot enums. These are diagnostic labels, not approval
     * to treat equipment slots, safe boxes or pockets as writable destinations. */
    PCTable types = pc_load(&ctx, slot_types);
    int64_t bag = -1, rig = -1;
    if (!pc_integer(pc_field(&types, "BagContainer"), &bag) || bag <= 0 ||
        !pc_integer(pc_field(&types, "ChestHangingContainer"), &rig) || rig <= 0 || bag == rig)
        ctx.complete = 0;
    const char *names[] = {"BagContainer", "ChestHangingContainer", "NearbyContainer", "DeadbodyLootBox", "SafeBoxContainer", "Pocket"};
    for (unsigned j = 0; j < sizeof(names)/sizeof(names[0]); j++) {
        int64_t n; if (pc_integer(pc_field(&types, names[j]), &n) && pc_emit(&ctx))
            printf("{\"record\":\"slotEnum\",\"name\":\"%s\",\"value\":%ld}\n", names[j], n);
        else ctx.complete = 0;
    }
    pc_free(&types);
    const char *names_group[] = {"Player", "Nearby", "DeadBody"};
    for (unsigned i = 0; i < 3; i++) {
        int64_t group;
        if (!pc_integer(pc_field(&enums, names_group[i]), &group) || group < 0 || group > 128) {
            ctx.complete = 0; continue;
        }
        LVValue items = pc_numeric(&item_groups, group), spaces = pc_numeric(&slot_groups, group);
        if (!pc_emit(&ctx)) break;
        printf("{\"record\":\"group\",\"name\":\"%s\",\"value\":%ld,\"itemsAvailable\":%s,\"slotsAvailable\":%s}\n",
               names_group[i], group, items.tag == 69 ? "true" : "false", spaces.tag == 69 ? "true" : "false");
        if (items.tag == 69) pc_items(&ctx, items.value, names_group[i]);
        if (spaces.tag == 69 && (!strcmp(names_group[i], "Player") ? bag > 0 && rig > 0 && bag != rig : 1))
            pc_slots(&ctx, spaces.value, names_group[i], bag, rig);
        /* A missing group is not equivalent to a known empty inventory. */
        if (items.tag != 69 || spaces.tag != 69) ctx.complete = 0;
    }
    pc_free(&enums); pc_free(&item_groups); pc_free(&slot_groups);
    for (unsigned i = 0; i < ctx.count; i++) {
        unsigned char now[32];
        if (!lv_read(&loot_values, ctx.guards[i].address, now, sizeof(now)) ||
            memcmp(now + 10, ctx.guards[i].header + 10, 22)) ctx.complete = 0;
    }
    if (lv_ptr(&loot_values, ue4_base + 0x1fc9cd70) != vm) ctx.complete = 0;
    printf("{\"record\":\"pickupContextSummary\",\"complete\":%s,\"actionReady\":false,\"pricesAvailable\":%s,\"tablesCopied\":%u,\"dataRecords\":%u,\"readsRemaining\":%u}\n",
           ctx.complete ? "true" : "false", loot_values.node_count ? "true" : "false", ctx.count, ctx.records, loot_values.read_budget);
    return 1;
}
#endif
