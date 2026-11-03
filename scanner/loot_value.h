/* Read-only loot metadata for Proxima 1.202.37117.5601.
 * Lua structures were validated against the live main-thread registry. Ordinary
 * collectibles use ShopServer's normal dynamic guide prices, the source used
 * by ItemBase.GetSingleSellPrice. Never substitute static guide/auction prices. */
#ifndef DELTA_LOOT_VALUE_H
#define DELTA_LOOT_VALUE_H
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define LV_MAX_NODES 8192
#define LV_ACTORS 512
#define LV_PRICE_SOURCE "shop-dynamic-guide"
typedef int (*LVRead)(void *, uint64_t, void *, size_t);
typedef struct { uint64_t value; int32_t tag, padding; } LVValue;
typedef struct { LVValue value; uint64_t key; int32_t key_tag, next; } LVNode;
typedef struct {
    uint64_t item_id;
    int64_t unit_value;
    int value_known, contents_known, container;
} LootValue;
typedef struct { uint64_t actor, checked; int kind; LootValue result; } LVActor;
typedef struct { uint64_t cls; int32_t index; int kind; } LVType;
typedef struct {
    LVRead read; void *user;
    uint64_t base, vm, map, refreshed, discovered, world;
    LVNode nodes[LV_MAX_NODES]; unsigned node_count;
    LVActor actors[LV_ACTORS]; unsigned next_actor;
    LVType types[128]; unsigned type_count, next_type;
    unsigned read_budget; int budget_enabled;
} LootValueReader;

static void lv_begin_frame(LootValueReader *r) { r->read_budget = 4096; r->budget_enabled = 1; }

static int lv_pointer(uint64_t p) { return p > 0x10000 && p < UINT64_C(0x10000000000); }
static int lv_read(LootValueReader *r, uint64_t p, void *out, size_t n) {
    if (r->budget_enabled) {
        if (!r->read_budget) return 0;
        r->read_budget--;
    }
    return lv_pointer(p) && n && n <= sizeof(r->nodes) && r->read(r->user, p, out, n);
}
static uint64_t lv_ptr(LootValueReader *r, uint64_t p) {
    uint64_t value = 0; lv_read(r, p, &value, sizeof(value)); return value;
}
/* Native class and immediate superclass IDs are verified together. This also
 * admits linked pickups which are not separately enumerated in level arrays. */
static int lv_kind(LootValueReader *r, uint64_t actor) {
    uint64_t original = lv_ptr(r, actor + 8), cls = original;
    int32_t original_index = 0;
    if (!lv_pointer(cls) || !lv_read(r, cls + 0x24, &original_index, 4)) return 0;
    for (unsigned i = 0; i < r->type_count; i++)
        if (r->types[i].cls == cls && r->types[i].index == original_index) return r->types[i].kind;
    int kind = 0;
    for (int depth = 0; depth < 16 && lv_pointer(cls); depth++) {
        int32_t index = 0, parent_index = 0; uint64_t parent = lv_ptr(r, cls + 0x40);
        if (!lv_read(r, cls + 0x24, &index, 4) || !lv_read(r, parent + 0x24, &parent_index, 4)) break;
        if (index == 7826 && parent_index == 7825) kind = 4;
        else if (index == 7825 && parent_index == 7824) kind = 2;
        else if (index == 7824 && parent_index == 7823) kind = 1;
        else if (index == 7559 && parent_index == 7557) kind = 3;
        else if (index == 6322 && parent_index == 57) kind = 5;
        if (kind || parent == cls) break;
        cls = parent;
    }
    if (!r->budget_enabled || r->read_budget) {
        unsigned slot = r->type_count < 128 ? r->type_count++ : r->next_type++ % 128;
        r->types[slot] = (LVType){original, original_index, kind};
    }
    return kind;
}
static int lv_table_header(LootValueReader *r, uint64_t p, unsigned *count, uint64_t *nodes) {
    unsigned char head[32];
    if (!lv_read(r, p, head, sizeof(head)) || head[8] != 5 || head[11] > 13) return 0;
    *count = 1U << head[11]; memcpy(nodes, head + 24, 8);
    return lv_pointer(*nodes);
}
static int lv_string_equal(LootValueReader *r, uint64_t p, int tag, const char *wanted) {
    unsigned char head[24]; size_t n = strlen(wanted); uint64_t length = 0;
    if ((tag != 68 && tag != 84) || n > 63 || !lv_read(r, p, head, sizeof(head))) return 0;
    if (tag == 68) length = head[11]; else memcpy(&length, head + 16, 8);
    char text[64];
    return length == n && lv_read(r, p + 24, text, n) && !memcmp(text, wanted, n);
}
static int lv_key_id(const LVNode *node, uint64_t *id) {
    if (node->key_tag == 19) { *id = node->key; return *id > 0; }
    if (node->key_tag == 3) {
        double d; memcpy(&d, &node->key, 8);
        if (!isfinite(d) || d < 1 || d > 9007199254740991.0 || floor(d) != d) return 0;
        *id = (uint64_t)d; return 1;
    }
    return 0;
}
static int lv_find(LootValueReader *r, uint64_t table, const char *key, int zero_key, LVValue *out) {
    unsigned count; uint64_t address;
    if (!lv_table_header(r, table, &count, &address)) return 0;
    LVNode small[8], *nodes = count <= 8 ? small : malloc(count * sizeof(*nodes));
    if (!nodes) return 0;
    int found = 0;
    if (lv_read(r, address, nodes, count * sizeof(*nodes))) {
        for (unsigned i = 0; i < count; i++) {
            LVNode *n = &nodes[i];
            if (!n->value.tag) continue;
            int match = zero_key ?
                ((n->key_tag == 19 && n->key == 0) ||
                 (n->key_tag == 3 && n->key == 0)) : lv_string_equal(r, n->key, n->key_tag, key);
            if (match) { *out = n->value; found = 1; break; }
        }
    }
    if (nodes != small) free(nodes);
    return found;
}
static uint64_t lv_table_field(LootValueReader *r, uint64_t table, const char *key) {
    LVValue v; return lv_find(r, table, key, 0, &v) && v.tag == 69 ? v.value : 0;
}
static void lv_refresh(LootValueReader *r, uint64_t now) {
    if (r->refreshed && now - r->refreshed < 1000) return;
    r->refreshed = now; r->node_count = 0;
    uint64_t vm = lv_ptr(r, r->base + 0x1fc9cd70);
    unsigned char type = 0;
    if (!lv_read(r, vm + 8, &type, 1) || type != 8) { r->map = 0; return; }
    if (vm != r->vm) { r->vm = vm; r->map = 0; r->discovered = 0; }
    if (!r->map || now - r->discovered >= 5000) {
        r->discovered = now;
        uint64_t g = lv_ptr(r, vm + 24); LVValue registry, roots[2];
        if (!lv_read(r, g + 0x40, &registry, sizeof(registry)) || registry.tag != 69 ||
            !lv_read(r, lv_ptr(r, registry.value + 16), roots, sizeof(roots)) ||
            roots[0].tag != 72 || roots[0].value != vm || roots[1].tag != 69) { r->map = 0; return; }
        uint64_t server = lv_table_field(r, roots[1].value, "Server");
        uint64_t shop = lv_table_field(r, server, "ShopServer");
        LVValue shop_type;
        if (!lv_find(r, shop, "_shopType", 0, &shop_type) ||
            !lv_string_equal(r, shop_type.value, shop_type.tag, "Normal")) { r->map = 0; return; }
        uint64_t data = lv_table_field(r, shop, "_data");
        uint64_t normal = lv_table_field(r, data, "Normal");
        r->map = lv_table_field(r, normal, "_mapItemId2DynamicGuidePrice");
    }
    unsigned count = 0; uint64_t nodes = 0;
    if (lv_table_header(r, r->map, &count, &nodes) &&
        lv_read(r, nodes, r->nodes, count * sizeof(*r->nodes))) r->node_count = count;
    else r->map = 0;
}
static int lv_unit_price(LootValueReader *r, uint64_t id, int64_t *price) {
    for (unsigned i = 0; i < r->node_count; i++) {
        LVNode *node = &r->nodes[i]; uint64_t key = 0;
        if (!lv_key_id(node, &key) || key != id) continue;
        LVValue amount = node->value;
        if (amount.tag == 19 && amount.value > 0 && amount.value <= 1000000000) {
            *price = (int64_t)amount.value; return 1;
        }
        if (amount.tag == 3) {
            double d; memcpy(&d, &amount.value, 8);
            if (isfinite(d) && d > 0 && d <= 1000000000 && floor(d) == d) {
                *price = (int64_t)d; return 1;
            }
        }
        return 0;
    }
    return 0;
}
static void lv_consider_item(LootValueReader *r, uint64_t item, LootValue *out) {
    if (r->budget_enabled && !r->read_budget) return;
    uint32_t pair[2]; int32_t count = 0;
    if (!lv_read(r, item + 0x10, pair, sizeof(pair)) || !pair[0] || pair[1] >= 10000 ||
        !lv_read(r, item + 0x38, &count, sizeof(count)) || count <= 0 || count > 100000) return;
    uint64_t id = (uint64_t)pair[0] * 10000 + pair[1]; int64_t price;
    if (!out->item_id) out->item_id = id;
    /* Runtime enums: CollectableItem=15; ordinary subtypes 1..9; Key=5.
     * Keys, equipment and special collectibles may need durability, attachment
     * or currency adjustments. Keep those unknown until independently verified. */
    unsigned subtype = (pair[0] / 1000) % 100;
    if (pair[0] / 100000 != 15 || subtype < 1 || subtype > 9 || subtype == 5) return;
    if (lv_unit_price(r, id, &price) && (!out->value_known || price > out->unit_value)) {
        out->item_id = id; out->unit_value = price; out->value_known = 1;
    }
}
typedef struct { uint64_t seen[128]; unsigned count; } LVWalk;
static void lv_merge(LootValue *out, LootValue child) {
    out->contents_known |= child.contents_known;
    if (child.value_known && (!out->value_known || child.unit_value > out->unit_value)) {
        out->item_id = child.item_id; out->unit_value = child.unit_value; out->value_known = 1;
    } else if (!out->item_id) out->item_id = child.item_id;
}
static LootValue lv_inspect(LootValueReader *r, uint64_t actor, int kind, unsigned depth, LVWalk *walk) {
    LootValue result = {.container = kind >= 2};
    if (!kind || depth >= 4 || walk->count >= 128 || !lv_pointer(actor)) return result;
    for (unsigned i = 0; i < walk->count; i++) if (walk->seen[i] == actor) return result;
    walk->seen[walk->count++] = actor;
    if (kind == 1 || kind == 2 || kind == 4) {
        unsigned char hidden = 0;
        if (!lv_read(r, actor + 0xfc0, &hidden, 1) || hidden != 0) return result;
    }
    if (kind == 1) {
        result.contents_known = 1;
        lv_consider_item(r, actor + 0x1240, &result);
    } else if (kind == 2 || kind == 4) {
        struct { uint64_t data; int32_t count, capacity; } items;
        unsigned char searched = 0, empty = 0;
        if (!lv_read(r, actor + 0x2040, &items, sizeof(items)) ||
            items.count < 0 || items.count > 128 || items.capacity < items.count ||
            items.capacity > 1024 || (items.count && !lv_pointer(items.data))) return result;
        lv_read(r, actor + 0x2068, &searched, 1); lv_read(r, actor + 0x2428, &empty, 1);
        result.contents_known = items.count > 0 || searched == 1 || empty == 1;
        for (int i = 0; i < items.count; i++) lv_consider_item(r, items.data + (uint64_t)i * 0x800, &result);
    } else if (kind == 3) {
        /* Reflected Interactor_SingleItemContainer layout, verified on device. */
        struct { uint64_t data; int32_t count, capacity; } children;
        if (lv_read(r, actor + 0x1128, &children, sizeof(children)) && children.count >= 0 &&
            children.count <= 128 && children.capacity >= children.count && children.capacity <= 1024 &&
            (!children.count || lv_pointer(children.data))) {
            for (int i = 0; i < children.count && walk->count < 128; i++) {
                uint64_t child = lv_ptr(r, children.data + (uint64_t)i * 8);
                lv_merge(&result, lv_inspect(r, child, lv_kind(r, child), depth + 1, walk));
            }
        }
        uint64_t open_box = lv_ptr(r, actor + 0x1180);
        lv_merge(&result, lv_inspect(r, open_box, lv_kind(r, open_box), depth + 1, walk));
    } else if (kind == 5) {
        /* ChangeableContainer.CacheSpawnActor; preview actors are not loot. */
        uint64_t child = lv_ptr(r, actor + 0x378);
        lv_merge(&result, lv_inspect(r, child, lv_kind(r, child), depth + 1, walk));
    }
    return result;
}
static LootValue lv_actor(LootValueReader *r, uint64_t actor, int kind, uint64_t now) {
    LVActor *slot = NULL;
    for (unsigned i = 0; i < LV_ACTORS; i++) if (r->actors[i].actor == actor && r->actors[i].kind == kind) { slot = &r->actors[i]; break; }
    if (slot && slot->checked && now - slot->checked < 500) return slot->result;
    if (!slot) slot = &r->actors[r->next_actor++ % LV_ACTORS];
    *slot = (LVActor){.actor = actor, .checked = now, .kind = kind};
    lv_refresh(r, now);
    LVWalk walk = {0};
    slot->result = lv_inspect(r, actor, kind, 0, &walk);
    if (r->budget_enabled && !r->read_budget) slot->checked = 0;
    return slot->result;
}
#endif
