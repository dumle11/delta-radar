/* One-shot, bounded loot metadata inspection. No remote calls or writes.
 * Uses reflected properties rather than assuming the dumped actor layout.
 * Raw diagnostic identifiers/addresses must stay in ignored local evidence. */
#define main scanner_main
#include "../memscan.c"
#undef main
#include <ctype.h>

typedef struct { uint64_t field, type; int offset, size; } LootProperty;
static uint64_t object_chunks;
static int object_count;
static int contains_case_insensitive(const char *text, const char *filter);

static uint64_t object_at(int index) {
    if (index < 0 || index >= object_count) return 0;
    uint64_t chunk = read_ptr(object_chunks + (uint64_t)(index / 65536) * 8);
    if (!is_any_ptr(chunk)) return 0;
    uint64_t obj = read_ptr(chunk + (uint64_t)(index % 65536) * 0x18);
    return is_any_ptr(obj) && read_i32(obj + 0x24) == index ? obj : 0;
}

static int init_probe(void) {
    game_pid = find_game_pid(); use_vm_readv = 1;
    if (game_pid <= 0 || parse_maps() || !ue4_base) return 0;
    char name[32] = {0};
    /* The canonical pool's globals can be write-only to process_vm_readv.
     * A read-only proc descriptor permits inspection without changing VMAs.
     * The older supposed readable alias is unrelated text and is not used. */
    for (int attempt = 0; attempt < 2; attempt++) {
        memset(fname_block_cache, 0, sizeof(fname_block_cache));
        fname_pool_addr = ue4_base + OFF_GNAMES_FROM_UE4_BASE;
        if (resolve_fname(0, name, sizeof(name)) && !strcmp(name, "None")) break;
        fname_pool_addr = 0;
        if (!attempt) {
            char path[80]; snprintf(path, sizeof(path), "/proc/%d/mem", game_pid);
            mem_fd = open(path, O_RDONLY | O_CLOEXEC);
            if (mem_fd < 0) break;
            use_vm_readv = 0;
        }
    }
    uint64_t objects = ue4_base + OFF_GUOBJECTARRAY_FROM_BASE;
    object_count = read_i32(objects + 0x14);
    object_chunks = read_ptr(objects + 0x20);
    fprintf(stderr, "loot-probe pid=%d names=%s objects=%d reads=%s\n", game_pid,
            fname_pool_addr ? "available" : "unavailable", object_count,
            use_vm_readv ? "vm_readv" : "proc-read-only");
    return fname_pool_addr && object_count > 0 && object_count <= 2000000 &&
           is_any_ptr(object_chunks);
}

static int property(uint64_t structure, const char *wanted, LootProperty *out) {
    for (int depth = 0; depth < 16 && is_any_ptr(structure); depth++) {
        uint64_t field = read_ptr(structure + 0x68);
        for (int count = 0; count < 512 && is_any_ptr(field); count++) {
            char name[128] = {0};
            if (resolve_fname(read_i32(field + 0x28), name, sizeof(name)) &&
                !strcmp(name, wanted)) {
                out->field = field; out->type = read_ptr(field + 0x88);
                out->offset = read_i32(field + 0x4c);
                out->size = read_i32(field + 0x3c);
                return out->offset >= 0 && out->offset <= 0x10000 &&
                       out->size > 0 && out->size <= 0x10000;
            }
            uint64_t next = read_ptr(field + 0x18);
            if (next == field) break;
            field = next;
        }
        uint64_t parent = read_ptr(structure + 0x40);
        if (parent == structure) break;
        structure = parent;
    }
    return 0;
}

static void print_schema(uint64_t structure) {
    char name[128] = {0};
    resolve_fname(read_i32(structure + OFF_UOBJECT_NAME), name, sizeof(name));
    printf("schema name=%s size=%d index=%d\n", name,
           read_i32(structure + 0x3c), read_i32(structure + 0x24));
    uint64_t field = read_ptr(structure + 0x68);
    for (int count = 0; count < 256 && is_any_ptr(field); count++) {
        char field_name[128] = {0}, type_name[128] = {0};
        resolve_fname(read_i32(field + 0x28), field_name, sizeof(field_name));
        resolve_fname(read_i32(read_ptr(field + 0x20)), type_name, sizeof(type_name));
        printf(" field name=%s type=%s offset=%x size=%d\n", field_name,
               type_name, read_i32(field + 0x4c), read_i32(field + 0x3c));
        uint64_t next = read_ptr(field + 0x18);
        if (next == field) break;
        field = next;
    }
}

static void number_field(uint64_t obj, uint64_t cls, const char *name) {
    LootProperty p;
    if (property(cls, name, &p) && (p.size == 1 || p.size == 4)) {
        unsigned char byte = 0; int32_t value = 0;
        ssize_t n = p.size == 1 ? read_mem(obj + p.offset, &byte, 1) :
                                read_mem(obj + p.offset, &value, 4);
        if (n == p.size) printf(",\"%s\":%d", name, p.size == 1 ? byte : value);
    }
}

static void item_info(uint64_t info, uint64_t structure) {
    LootProperty id;
    if (!property(structure, "ItemID", &id)) { printf("{\"known\":false}"); return; }
    uint32_t pair[2] = {0};
    if (read_mem(info + id.offset, pair, sizeof(pair)) != sizeof(pair)) {
        printf("{\"known\":false}"); return;
    }
    printf("{\"known\":%s,\"category\":%u,\"sequence\":%u",
           pair[0] && pair[1] < 10000 ? "true" : "false", pair[0], pair[1]);
    number_field(info, structure, "ItemCount");
    number_field(info, structure, "IsInContainer");
    number_field(info, structure, "ItemState");
    printf("}");
}

/* Diagnostic only: replacement definitions are not proven current contents. */
static void replacement_drops(uint64_t obj, uint64_t cls) {
    LootProperty array, id, count, price, sell;
    if (!property(cls, "DropItems_ForReplace", &array) || array.size != 16) return;
    uint64_t inner = read_ptr(array.field + 0x88), type = read_ptr(inner + 0x88);
    int stride = read_i32(inner + 0x3c);
    struct { uint64_t data; int32_t count, capacity; } header = {0};
    int header_readable = read_mem(obj + array.offset, &header, sizeof(header)) == sizeof(header);
    uint64_t data = header.data;
    int n = header.count, capacity = header.capacity;
    int valid = header_readable && is_any_ptr(inner) && is_any_ptr(type) &&
                stride >= 0x38 && stride <= 0x400 && n >= 0 && n <= 128 &&
                capacity >= n && capacity <= 1024 && (!n || is_any_ptr(data)) &&
                property(type, "ItemID", &id) && id.size == 8 && id.offset <= stride - 8 &&
                property(type, "Number", &count) && count.size == 4 && count.offset <= stride - 4 &&
                property(type, "Price", &price) && price.size == 4 && price.offset <= stride - 4 &&
                property(type, "SellPrice", &sell) && sell.size == 4 && sell.offset <= stride - 4;
    printf(",\"replacementDropsValid\":%s", valid ? "true" : "false");
    if (!valid) return;
    printf(",\"replacementDrops\":[");
    for (int i = 0; i < n; i++) {
        uint64_t entry = data + (uint64_t)i * stride, item = 0;
        uint32_t number = 0, amount = 0, sell_amount = 0;
        int readable = read_mem(entry + id.offset, &item, 8) == 8 &&
                       read_mem(entry + count.offset, &number, 4) == 4 &&
                       read_mem(entry + price.offset, &amount, 4) == 4 &&
                       read_mem(entry + sell.offset, &sell_amount, 4) == 4;
        printf("%s{\"readable\":%s,\"itemId\":\"%llu\",\"count\":%u,"
               "\"price\":%u,\"sellPrice\":%u}", i ? "," : "",
               readable ? "true" : "false", (unsigned long long)item, number, amount, sell_amount);
    }
    putchar(']');
}

/* The SDK leaves most ItemSpace members opaque. Capture only this named,
 * class-verified bounded object for offline comparison, never follow its
 * arbitrary pointers or interpret opaque bytes as current item contents. */
static void container_space(uint64_t obj, uint64_t cls) {
    LootProperty p;
    if (!property(cls, "ContainerSpace", &p) || p.size != 8) return;
    uint64_t space = read_ptr(obj + p.offset), type = read_ptr(space + OFF_UOBJECT_CLASS);
    char name[96] = {0};
    resolve_class_name(type, name, sizeof(name));
    int size = read_i32(type + 0x3c);
    printf(",\"containerSpace\":{\"object\":\"%lx\",\"classVerified\":%s",
           space, is_any_ptr(space) && type == p.type && !strcmp(name, "ItemSpace") ? "true" : "false");
    if (is_any_ptr(space) && type == p.type && !strcmp(name, "ItemSpace") &&
        size >= 0x80 && size <= 1024) {
        unsigned char bytes[1024];
        int readable = read_mem(space, bytes, (size_t)size) == size;
        printf(",\"size\":%d,\"readable\":%s", size, readable ? "true" : "false");
        if (readable) {
            printf(",\"opaqueBytes\":\"");
            for (int i = 0; i < size; i++) printf("%02x", bytes[i]);
            putchar('"');
        }
    }
    putchar('}');
}

static void actor(uint64_t obj) {
    uint64_t cls = read_ptr(obj + OFF_UOBJECT_CLASS);
    int kind = loot_kind_from_class(cls);
    if (!kind) return;
    char class_name[128] = {0}, escaped[256];
    resolve_class_name(cls, class_name, sizeof(class_name));
    json_escape(class_name, escaped, sizeof(escaped));
    printf("{\"actor\":\"%lx\",\"class\":\"%s\",\"kind\":%d", obj, escaped, kind);
    LootProperty p;
    if (property(cls, "InventoryGID", &p) && p.size == 8) {
        uint64_t gid = 0;
        if (read_mem(obj + p.offset, &gid, sizeof(gid)) == sizeof(gid))
            printf(",\"inventoryGid\":\"%llu\"", (unsigned long long)gid);
    }
    if (property(cls, "InventoryIdName", &p)) {
        char name[128] = {0};
        if (resolve_fname(read_i32(obj + p.offset), name, sizeof(name))) {
            json_escape(name, escaped, sizeof(escaped));
            printf(",\"itemNameId\":\"%s\"", escaped);
        }
    }
    const char *fields[] = {"StackCount", "bIsHiddenItem", "bClientSearchDone",
                           "bIsEmpty", "bFirstOpened", "bRandomEnabled",
                           "bFinished", "bSafeBoxUnlocked", "bRandomContainer",
                           "boxId", "SafeBoxId"};
    for (unsigned i = 0; i < sizeof(fields)/sizeof(fields[0]); i++)
        number_field(obj, cls, fields[i]);
    if (property(cls, "PickupItemInfo", &p)) {
        printf(",\"pickupItem\":"); item_info(obj + p.offset, p.type);
    }
    if (property(cls, "RepItemArray", &p)) {
        LootProperty items;
        if (property(p.type, "Items", &items)) {
            uint64_t arr = obj + p.offset + items.offset, data = read_ptr(arr);
            int count = read_i32(arr + 8), capacity = read_i32(arr + 12);
            uint64_t inner = read_ptr(items.field + 0x88);
            int stride = read_i32(inner + 0x3c);
            uint64_t info_type = read_ptr(inner + 0x88);
            int valid = count >= 0 && count <= 128 && capacity >= count &&
                        capacity <= 1024 && stride > 0 && stride <= 0x10000 &&
                        (count == 0 || is_any_ptr(data));
            printf(",\"contentsValid\":%s", valid ? "true" : "false");
            if (valid) {
                printf(",\"contentsCount\":%d,\"items\":[", count);
                for (int i = 0; i < count; i++) {
                    if (i) putchar(',');
                    item_info(data + (uint64_t)i * stride, info_type);
                }
                putchar(']');
            }
        }
    }
    if (property(cls, "CachedPickups", &p)) {
        uint64_t arr = obj + p.offset, data = read_ptr(arr);
        int count = read_i32(arr + 8), capacity = read_i32(arr + 12);
        if (count >= 0 && count <= 128 && capacity >= count && capacity <= 1024 &&
            (!count || is_any_ptr(data))) {
            printf(",\"cachedPickups\":[");
            for (int i = 0; i < count; i++)
                printf("%s\"%lx\"", i ? "," : "", read_ptr(data + i * 8));
            putchar(']');
        }
    }
    const char *links[] = {"CachedOpenBox", "CacheSpawnActor", "RootComponent"};
    for (unsigned i = 0; i < sizeof(links) / sizeof(links[0]); i++)
        if (property(cls, links[i], &p) && p.size == 8)
            printf(",\"%s\":\"%lx\"", links[i], read_ptr(obj + p.offset));
    replacement_drops(obj, cls);
    container_space(obj, cls);
    int metadata_kind = lv_kind(&loot_values, obj);
    LootValue value = lv_actor(&loot_values, obj, metadata_kind, diag_now_ms());
    printf(",\"metadataKind\":%d,\"candidateValueKnown\":%s,\"candidateUnitValue\":%lld",
           metadata_kind, value.value_known ? "true" : "false", (long long)value.unit_value);
    printf("}\n");
}

/* Independent of production's level-array/position de-duplication. A single
 * bounded UObject pass can find named closed containers missing from its feed.
 * Coordinates are supplied from a fresh frame, in metres. No pawn fields are
 * output. The existing eight-second process deadline still applies. */
static uint64_t owning_world(uint64_t obj) {
    for (int depth = 0; depth < 16 && is_any_ptr(obj); depth++) {
        char name[96] = {0};
        uint64_t cls = read_ptr(obj + OFF_UOBJECT_CLASS);
        resolve_class_name(cls, name, sizeof(name));
        if (!strcmp(name, "World")) return obj;
        /* Streamed levels can have a different package World as their Outer.
         * The reflected OwningWorld is the active game World instead. */
        if (!strcmp(name, "Level")) {
            LootProperty field;
            if (property(cls, "OwningWorld", &field) && field.size == 8) {
                uint64_t world = read_ptr(obj + field.offset);
                char world_name[96] = {0};
                resolve_class_name(read_ptr(world + OFF_UOBJECT_CLASS), world_name, sizeof(world_name));
                if (!strcmp(world_name, "World")) return world;
            }
        }
        uint64_t outer = read_ptr(obj + OFF_UOBJECT_OUTER);
        if (outer == obj) break;
        obj = outer;
    }
    return 0;
}

static int nearby_loot(float x, float y, float z, float radius, const char *filter, uint64_t anchor) {
    struct { uint64_t cls; int kind; } cache[2048] = {{0}};
    LootValueReader types = {.read = read_loot_value_memory, .base = ue4_base};
    struct { uint64_t obj; float distance; } nearest[32] = {{0}};
    uint64_t unresolved[16] = {0};
    int count = 0, unresolved_count = 0, invalid_roots = 0, matched = 0;
    uint64_t world = 0;
    if (anchor) {
        if (object_at(read_i32(anchor + 0x24)) != anchor || !(world = owning_world(anchor))) return 0;
    }
    int limit = object_count < 300000 ? object_count : 300000;
    for (int index = 0; index < limit; index++) {
        uint64_t obj = object_at(index);
        if (!obj) continue;
        uint64_t cls = read_ptr(obj + OFF_UOBJECT_CLASS);
        if (!is_any_ptr(cls)) continue;
        unsigned slot = (unsigned)((cls >> 4) ^ (cls >> 19)) & 2047;
        if (cache[slot].cls != cls) {
            cache[slot].cls = cls; cache[slot].kind = lv_kind(&types, obj);
        }
        if (!cache[slot].kind) continue;
        /* Class default objects are templates, not map loot. */
        if (read_ptr(cls + 0x138) == obj) continue;
        char name[128] = {0}; resolve_class_name(cls, name, sizeof(name));
        if (filter && !contains_case_insensitive(name, filter)) continue;
        if (world && owning_world(obj) != world) continue;
        matched++;
        uint64_t root = read_ptr(obj + OFF_AACTOR_ROOT_COMPONENT);
        float pos[3] = {0};
        if (!is_any_ptr(root) || !read_component_position(root, OFF_USCENE_WORLD_LOCATION, pos) ||
            !isfinite(pos[0]) || !isfinite(pos[1]) || !isfinite(pos[2]) ||
            fabsf(pos[0]) > 10000000 || fabsf(pos[1]) > 10000000 || fabsf(pos[2]) > 10000000 ||
            (pos[0] == 0 && pos[1] == 0 && pos[2] == 0)) {
            invalid_roots++;
            if (filter && unresolved_count < 16) unresolved[unresolved_count++] = obj;
            continue;
        }
        float dx = pos[0] / 100 - x, dy = pos[1] / 100 - y, dz = pos[2] / 100 - z;
        float distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (!isfinite(distance) || distance > radius) continue;
        int slot_index = count < 32 ? count++ : 31;
        if (count == 32 && nearest[31].obj && distance >= nearest[31].distance) continue;
        while (slot_index > 0 && distance < nearest[slot_index - 1].distance) {
            nearest[slot_index] = nearest[slot_index - 1]; slot_index--;
        }
        nearest[slot_index].obj = obj; nearest[slot_index].distance = distance;
    }
    for (int i = 0; i < count; i++) {
        printf("nearby distance=%.3f\n", nearest[i].distance); actor(nearest[i].obj);
    }
    for (int i = 0; i < unresolved_count; i++) {
        puts("nearby unresolved-transform"); actor(unresolved[i]);
    }
    printf("nearby-complete inspected=%d total=%d truncated=%s matched=%d invalidRoots=%d "
           "emitted=%d unresolvedEmitted=%d metadataReadsRemaining=%u worldFiltered=%s\n", limit, object_count,
           limit == object_count ? "false" : "true", matched, invalid_roots, count,
           unresolved_count, loot_values.read_budget, world ? "true" : "false");
    return 1;
}

static void tables(void) {
    /* Traverse the bounded UObject registry once, never the address space. */
    int limit = object_count < 300000 ? object_count : 300000;
    for (int index = 0; index < limit; index++) {
        uint64_t obj = object_at(index);
        if (!obj) continue;
        uint64_t cls = read_ptr(obj + OFF_UOBJECT_CLASS);
        if (!is_any_ptr(cls) || read_i32(cls + 0x24) != 0x286 ||
            read_i32(read_ptr(cls + 0x40) + 0x24) != 1) continue;
        uint64_t row = read_ptr(obj + 0x28);
        int row_index = read_i32(row + 0x24);
        if (row_index != 0x7085 && row_index != 0x7088 && row_index != 0x6fb3) continue;
        char name[128] = {0}, row_name[128] = {0};
        resolve_fname(read_i32(obj + OFF_UOBJECT_NAME), name, sizeof(name));
        resolve_fname(read_i32(row + OFF_UOBJECT_NAME), row_name, sizeof(row_name));
        printf("table object=%lx name=%s row=%s rowSize=%d mapData=%lx slots=%d capacity=%d\n",
               obj, name, row_name, read_i32(row + 0x3c), read_ptr(obj + 0x30),
               read_i32(obj + 0x38), read_i32(obj + 0x3c));
    }
    fprintf(stderr, "registry-inspected=%d truncated=%s\n", limit,
            limit == object_count ? "false" : "true");
}

/* Native InventoryDataCenter::GetItemByGID disassembly identifies the TMap
 * at +0x48: uint64 GID -> shared FInventoryItemInfo, 32-byte sparse entries.
 * This is a separate diagnostic source, not an alternative production price. */
static int inventory_cache(void) {
    uint64_t cls = object_at(0x1e82);
    char name[96] = {0}; resolve_class_name(cls, name, sizeof(name));
    if (!cls || strcmp(name, "InventoryDataCenter") ||
        read_i32(read_ptr(cls + 0x40) + 0x24) != 0x1bae) return 0;
    uint64_t sol = object_at(0x29d3), prop_hunt = object_at(0x2875);
    if (read_ptr(sol + 0x40) != cls || read_ptr(prop_hunt + 0x40) != cls) return 0;
    unsigned emitted = 0, centers = 0;
    int limit = object_count < 300000 ? object_count : 300000;
    for (int index = 0; index < limit; index++) {
        uint64_t obj = object_at(index);
        if (!obj) continue;
        uint64_t actual_cls = read_ptr(obj + OFF_UOBJECT_CLASS);
        if (actual_cls != cls && actual_cls != sol && actual_cls != prop_hunt) continue;
        if (read_ptr(actual_cls + 0x138) == obj) continue;
        if (++centers > 16) return 0;
        unsigned char header[0x50], after[0x50];
        if (read_mem(obj + 0x48, header, sizeof(header)) != sizeof(header)) return 0;
        uint64_t data = 0, flags = 0;
        int32_t slots = 0, capacity = 0, bits = 0, free_count = 0;
        memcpy(&data, header, 8); memcpy(&slots, header + 8, 4);
        memcpy(&capacity, header + 12, 4); memcpy(&flags, header + 0x20, 8);
        memcpy(&bits, header + 0x28, 4); memcpy(&free_count, header + 0x34, 4);
        if (slots < 0 || slots > 16384 || capacity < slots || capacity > 65536 ||
            bits < slots || bits > 65536 || free_count < 0 || free_count > slots ||
            (slots && !is_any_ptr(data))) return 0;
        if (!flags) {
            if (bits > 128) return 0;
            flags = obj + 0x58;
        }
        if (!is_any_ptr(flags)) return 0;
        size_t n = (size_t)slots * 32, flag_n = ((size_t)slots + 31) / 32 * 4;
        unsigned char *entries = malloc(n ? n : 1);
        uint32_t *allocated = malloc(flag_n ? flag_n : 1);
        if (!entries || !allocated) { free(entries); free(allocated); return 0; }
        if ((n && read_mem(data, entries, n) != (ssize_t)n) ||
            (flag_n && read_mem(flags, allocated, flag_n) != (ssize_t)flag_n)) {
            free(entries); free(allocated); return 0;
        }
        unsigned active = 0, valid = 0, unreadable = 0, skipped = 0;
        for (int i = 0; i < slots; i++) {
            if (!(allocated[i / 32] & (1U << (i % 32)))) continue;
            active++;
            uint64_t gid = 0, info = 0, control = 0, stored_gid = 0;
            memcpy(&gid, entries + (size_t)i * 32, 8);
            memcpy(&info, entries + (size_t)i * 32 + 8, 8);
            memcpy(&control, entries + (size_t)i * 32 + 16, 8);
            unsigned char item[0x78]; uint32_t pair[2]; int32_t count = 0, attach = 0;
            if (!gid || !is_any_ptr(info) || !is_any_ptr(control) ||
                read_mem(info, item, sizeof(item)) != sizeof(item)) { unreadable++; continue; }
            memcpy(&stored_gid, item + 0x28, 8); memcpy(pair, item + 0x10, 8);
            memcpy(&count, item + 0x38, 4); memcpy(&attach, item + 0x70, 4);
            if (stored_gid != gid || !pair[0] || pair[1] >= 10000 ||
                count < 0 || count > 100000) { unreadable++; continue; }
            valid++;
            if (emitted >= 4096) { skipped++; continue; }
            emitted++;
            printf("{\"inventoryCacheItem\":true,\"center\":\"%lx\",\"gid\":\"%llu\","
                   "\"itemId\":\"%llu\",\"count\":%d,\"attachPosition\":%d}\n",
                   obj, (unsigned long long)gid,
                   (unsigned long long)((uint64_t)pair[0] * 10000 + pair[1]), count, attach);
        }
        free(entries); free(allocated);
        int stable = read_mem(obj + 0x48, after, sizeof(after)) == sizeof(after) &&
                     !memcmp(header, after, sizeof(header));
        printf("{\"inventoryCacheSummary\":true,\"center\":\"%lx\",\"slots\":%d,"
               "\"active\":%u,\"validItems\":%u,\"unreadable\":%u,\"skipped\":%u,"
               "\"allocationCountMatches\":%s,\"stableHeader\":%s}\n", obj, slots,
               active, valid, unreadable, skipped,
               active == (unsigned)(slots - free_count) ? "true" : "false", stable ? "true" : "false");
    }
    printf("inventory-cache-complete inspected=%d total=%d truncated=%s centers=%u emitted=%u\n",
           limit, object_count, limit == object_count ? "false" : "true", centers, emitted);
    return 1;
}

static int rows(uint64_t table, int argc, char **argv) {
    uint64_t cls = read_ptr(table + OFF_UOBJECT_CLASS), row = read_ptr(table + 0x28);
    int row_index = read_i32(row + 0x24);
    if (read_i32(cls + 0x24) != 0x286 ||
        (row_index != 0x7085 && row_index != 0x7088 && row_index != 0x6fb3)) return 0;
    uint64_t map = table + 0x30, data = read_ptr(map);
    int slots = read_i32(map + 8), capacity = read_i32(map + 12);
    int bits = read_i32(map + 0x28);
    uint64_t flags = read_ptr(map + 0x20);
    if (!flags) flags = map + 0x10;
    if (!is_any_ptr(data) || slots < 0 || slots > 50000 || capacity < slots ||
        capacity > 100000 || bits < slots || bits > 100000 || !is_any_ptr(flags)) return 0;
    size_t data_size = (size_t)slots * 24, flag_size = ((size_t)slots + 31) / 32 * 4;
    unsigned char *entries = malloc(data_size ? data_size : 1);
    uint32_t *allocated = malloc(flag_size ? flag_size : 1);
    if (!entries || !allocated) { free(entries); free(allocated); return 0; }
    if (read_mem(data, entries, data_size) != (ssize_t)data_size ||
        read_mem(flags, allocated, flag_size) != (ssize_t)flag_size) {
        free(entries); free(allocated); return 0;
    }
    int matches = 0;
    for (int i = 0; i < slots; i++) {
        if (!(allocated[i / 32] & (1U << (i % 32)))) continue;
        uint64_t value = 0; int32_t key_index = 0;
        memcpy(&key_index, entries + (size_t)i * 24, 4);
        memcpy(&value, entries + (size_t)i * 24 + 8, 8);
        char key[128] = {0};
        if (!is_any_ptr(value) || !resolve_fname(key_index, key, sizeof(key))) continue;
        for (int a = 0; a < argc; a++) {
            if (strcmp(key, argv[a])) continue;
            char escaped[256]; json_escape(key, escaped, sizeof(escaped));
            printf("{\"itemId\":\"%s\",\"source\":\"%s\"", escaped,
                   row_index == 0x7088 ? "ItemPriceTable" :
                   row_index == 0x6fb3 ? "AuctionProp" : "GameItem");
            if (row_index == 0x7088) {
                printf(",\"rowItemId\":%lu,\"price\":%u", read_ptr(value + 0x10),
                       (uint32_t)read_i32(value + 0x18));
            } else if (row_index == 0x6fb3) {
                printf(",\"rowItemId\":%lu", read_ptr(value + 0x10));
                number_field(value, row, "GuidePrice");
            } else {
                /* Native price lookup at module+0x1606a3c8 reads this
                 * unreflected slot before falling back to InitialGuidePrice.
                 * Keep it explicitly a candidate until compared with the UI. */
                printf(",\"cachedPriceCandidate\":%d", read_i32(value + 0x23c));
                const char *fields[] = {"Quality", "RecyclePrice", "InitialGuidePrice",
                                         "SellPriceRates", "MaxStackCount"};
                for (unsigned j = 0; j < sizeof(fields)/sizeof(fields[0]); j++)
                    number_field(value, row, fields[j]);

            }
            printf("}\n"); matches++; break;
        }
    }
    free(entries); free(allocated);
    fprintf(stderr, "rows-matched=%d slots=%d\n", matches, slots);
    return 1;
}

static void lua_price_metadata(void) {
    /* The page-in wrapper only reads this global; we do not invoke it.
     * Validate a registry by its main-thread entry before inspecting any keys. */
    uint64_t vm = read_ptr(ue4_base + 0x1fc9cd70), g = read_ptr(vm + 0x18);
    unsigned char vm_type = 0;
    read_mem(vm + 8, &vm_type, 1);
    printf("lua-metadata vm=%lx type=%u global=%lx\n", vm, vm_type, g);
    if (!is_game_heap_ptr(vm) || vm_type != 8 || !is_game_heap_ptr(g)) return;
    for (int off = 0x30; off <= 0x180; off += 8) {
        if (read_i32(g + off + 8) != 69) continue;
        uint64_t registry = read_ptr(g + off), array = read_ptr(registry + 0x10);
        int count = read_i32(registry + 0xc);
        if (!is_game_heap_ptr(registry) || count < 2 || count > 1000000 ||
            !is_game_heap_ptr(array) || read_ptr(array) != vm ||
            read_i32(array + 8) != 72 || read_i32(array + 24) != 69) continue;
        uint64_t globals = read_ptr(array + 16), node = read_ptr(globals + 0x18);
        unsigned char lsizenode = 0, table_type = 0;
        read_mem(globals + 8, &table_type, 1);
        read_mem(globals + 11, &lsizenode, 1);
        if (!is_game_heap_ptr(globals) || table_type != 5 ||
            lsizenode > 16 || !is_game_heap_ptr(node)) continue;
        printf("lua-registry offset=%x globals=%lx node=%lx nodeCount=%u\n",
               off, globals, node, 1U << lsizenode);
        for (unsigned i = 0; i < (1U << lsizenode); i++) {
            unsigned char bytes[32] = {0};
            if (read_mem(node + (uint64_t)i * 32, bytes, sizeof(bytes)) != sizeof(bytes)) continue;
            int tag = 0, key_tag = 0; uint64_t key = 0, value = 0;
            memcpy(&tag, bytes + 8, 4); memcpy(&key_tag, bytes + 24, 4);
            memcpy(&key, bytes + 16, 8); memcpy(&value, bytes, 8);
            if (!tag || key_tag != 68 || !is_game_heap_ptr(key)) continue;
            unsigned char len = 0; read_mem(key + 11, &len, 1);
            if (!len || len > 127) continue;
            char name[128] = {0};
            if (read_mem(key + 24, name, len) != len) continue;
            int valid = 1;
            for (unsigned j = 0; j < len; j++) if ((unsigned char)name[j] < 32 || (unsigned char)name[j] > 126) valid = 0;
            if (valid) printf("lua-global name=%s tag=%d value=%lx\n", name, tag, value);
        }
        return;
    }
}

static int lua_read_string(uint64_t string, int tag, char *out, size_t capacity) {
    if (!is_game_heap_ptr(string) || (tag != 68 && tag != 84)) return 0;
    unsigned char short_length = 0;
    if (read_mem(string + 11, &short_length, 1) != 1) return 0;
    uint64_t length = tag == 68 ? short_length : read_ptr(string + 16);
    if (!length || length >= capacity ||
        read_mem(string + 24, out, (size_t)length) != (ssize_t)length) return 0;
    out[length] = 0;
    for (size_t i = 0; i < length; i++)
        if ((unsigned char)out[i] < 32 || (unsigned char)out[i] > 126) return 0;
    return 1;
}

static int contains_case_insensitive(const char *text, const char *filter) {
    size_t length = strlen(filter);
    for (; *text; text++) {
        size_t j = 0;
        while (j < length && text[j] && tolower((unsigned char)text[j]) == tolower((unsigned char)filter[j])) j++;
        if (j == length) return 1;
    }
    return !length;
}

static void lua_print_value(const char *name, int tag, uint64_t value, int number_allowed) {
    char escaped[512]; json_escape(name, escaped, sizeof(escaped));
    printf("{\"key\":\"%s\",\"tag\":%d,\"value\":\"%lx\"", escaped, tag, value);
    if (number_allowed && tag == 19) printf(",\"number\":%ld", (int64_t)value);
    if (number_allowed && tag == 3) {
        double number = 0; memcpy(&number, &value, 8);
        if (isfinite(number)) printf(",\"number\":%.9g", number);
    }
    char value_text[256];
    if (lua_read_string(value, tag, value_text, sizeof(value_text))) {
        char value_escaped[512]; json_escape(value_text, value_escaped, sizeof(value_escaped));
        printf(",\"text\":\"%s\"", value_escaped);
    }
    printf("}\n");
}

/* Lua 5.3 layouts: lua/lua v5.3.6 lobject.h. This diagnostic reads a single
 * validated closure/prototype; it never calls Lua or changes the VM. */
static int lua_function(uint64_t closure) {
    unsigned char head[32];
    if (!is_game_heap_ptr(closure) || read_mem(closure, head, sizeof(head)) != sizeof(head) ||
        head[8] != 6 || head[10] > 64) return 0;
    uint64_t proto = read_ptr(closure + 24);
    unsigned char p[112];
    if (!is_game_heap_ptr(proto) || read_mem(proto, p, sizeof(p)) != sizeof(p) || p[8] != 9) return 0;
    int nk = 0, nc = 0, nu = 0; uint64_t constants = 0, code = 0;
    memcpy(&nu, p + 16, 4); memcpy(&nk, p + 20, 4); memcpy(&nc, p + 24, 4);
    memcpy(&constants, p + 48, 8); memcpy(&code, p + 56, 8);
    if (nu < 0 || nu > 64 || nk < 0 || nk > 512 || nc < 0 || nc > 4096 || nu != head[10]) return 0;
    printf("lua-function parameters=%u upvalues=%d constants=%d instructions=%d\n", p[10], nu, nk, nc);
    for (int i = 0; i < nk; i++) {
        uint64_t value = read_ptr(constants + (uint64_t)i * 16);
        int tag = read_i32(constants + (uint64_t)i * 16 + 8);
        char key[32], text[256]; snprintf(key, sizeof(key), "constant_%d", i);
        lua_print_value(key, tag, value, 1);
        if (lua_read_string(value, tag, text, sizeof(text))) {
            char escaped[512]; json_escape(text, escaped, sizeof(escaped));
            printf("{\"constant\":%d,\"text\":\"%s\"}\n", i, escaped);
        }
    }
    for (int i = 0; i < nu; i++) {
        uint64_t upval = read_ptr(closure + 32 + (uint64_t)i * 8), slot = read_ptr(upval);
        if (!is_game_heap_ptr(upval) || !is_game_heap_ptr(slot)) continue;
        char key[32]; snprintf(key, sizeof(key), "upvalue_%d", i);
        lua_print_value(key, read_i32(slot + 8), read_ptr(slot), 1);
    }
    for (int i = 0; i < nc; i++) printf("lua-instruction index=%d word=%08x\n", i, (unsigned)read_i32(code + (uint64_t)i * 4));
    puts("lua-function-complete");
    return 1;
}

static int lua_table(uint64_t table, const char *wanted, int exact) {
    unsigned char type = 0, log_count = 0;
    if (!is_game_heap_ptr(table) || read_mem(table + 8, &type, 1) != 1 || type != 5 ||
        read_mem(table + 11, &log_count, 1) != 1 || log_count > 16) return 0;
    uint64_t array = read_ptr(table + 0x10);
    int array_count = read_i32(table + 0xc);
    if (array_count < 0 || array_count > 1000000) return 0;
    if (wanted && exact && wanted[0] == '#') {
        char *end = NULL; unsigned long long index = strtoull(wanted + 1, &end, 10);
        if (end && !*end && index >= 1 && index <= (unsigned)array_count && is_game_heap_ptr(array)) {
            unsigned char bytes[16] = {0}; int tag = 0; uint64_t value = 0;
            if (read_mem(array + (index - 1) * 16, bytes, sizeof(bytes)) != sizeof(bytes)) return 0;
            memcpy(&value, bytes, 8); memcpy(&tag, bytes + 8, 4);
            if (tag) {
                lua_print_value(wanted, tag, value, 1);
                printf("lua-table-summary array=%d matches=1 metatable=%lx\n", array_count, read_ptr(table + 0x28));
                return 1;
            }
        }
    }
    if (!wanted && array_count && is_game_heap_ptr(array)) {
        for (int i = 0; i < array_count && i < 128; i++) {
            unsigned char bytes[16] = {0}; int tag = 0; uint64_t value = 0;
            if (read_mem(array + (uint64_t)i * 16, bytes, sizeof(bytes)) != sizeof(bytes)) return 0;
            memcpy(&value, bytes, 8); memcpy(&tag, bytes + 8, 4);
            char name[24]; snprintf(name, sizeof(name), "#%d", i + 1);
            if (tag) lua_print_value(name, tag, value, 0);
        }
    }
    uint64_t node = read_ptr(table + 0x18);
    if (!is_any_ptr(node)) return 0;
    int matches = 0;
    for (unsigned i = 0; i < (1U << log_count); i++) {
        unsigned char bytes[32] = {0};
        if (read_mem(node + (uint64_t)i * 32, bytes, sizeof(bytes)) != sizeof(bytes)) return 0;
        int tag = 0, key_tag = 0; uint64_t key = 0, value = 0;
        memcpy(&tag, bytes + 8, 4); memcpy(&key_tag, bytes + 24, 4);
        memcpy(&key, bytes + 16, 8); memcpy(&value, bytes, 8);
        if (!tag) continue;
        char name[256] = {0};
        if ((!wanted || wanted[0] == '#') && (key_tag == 19 || key_tag == 3)) {
            if (key_tag == 19) snprintf(name, sizeof(name), "#%ld", (int64_t)key);
            else if (key_tag == 3) {
                double number = 0; memcpy(&number, &key, 8);
                if (!isfinite(number) || number < 0 || number > 9007199254740991.0) continue;
                snprintf(name, sizeof(name), "#%.0f", number);
            } else continue;
        } else if (!wanted && (key_tag == 69 || key_tag == 71 || key_tag == 2)) {
            /* Sets of view instances use tables/userdata as keys. Keep their
             * identity opaque; string-key-only listing would look empty. */
            snprintf(name, sizeof(name), "@%d:%lx", key_tag, key);
        } else if (!lua_read_string(key, key_tag, name, sizeof(name))) continue;
        if (wanted && !(exact ? !strcmp(name, wanted) : contains_case_insensitive(name, wanted))) continue;
        lua_print_value(name, tag, value, exact);
        if (++matches >= 1024 || exact) break;
    }
    printf("lua-table-summary slots=%u matches=%d metatable=%lx\n",
           1U << log_count, matches, read_ptr(table + 0x28));
    return 1;
}

/* Lua 5.3 ARM64 Udata layout: metatable +16, length +24, payload +40.
 * https://www.lua.org/source/5.3/lobject.h.html#Udata
 * Bounded inspection only. Candidate UObject pointers must agree with the
 * current registry slot; opaque wrapper bytes are never treated as commands. */
static int lua_userdata(uint64_t address) {
    unsigned char head[40], after[40], payload[256], check[256]; uint64_t length, meta;
    if (!lv_read(&loot_values, address, head, sizeof(head)) || head[8] != 7) return 0;
    memcpy(&meta, head + 16, 8); memcpy(&length, head + 24, 8);
    if (!length || length > sizeof(payload)) return 0;
    unsigned char kind;
    if (meta && (!lv_read(&loot_values, meta + 8, &kind, 1) || kind != 5)) return 0;
    if (!lv_read(&loot_values, address + 40, payload, (size_t)length) ||
        !lv_read(&loot_values, address, after, sizeof(after)) || after[8] != 7 ||
        memcmp(head + 10, after + 10, 30) ||
        !lv_read(&loot_values, address + 40, check, (size_t)length) || memcmp(payload, check, (size_t)length)) return 0;
    printf("{\"record\":\"userdata\",\"length\":%lu,\"metatable\":\"%lx\",\"payload\":\"", length, meta);
    for (unsigned i = 0; i < length; i++) printf("%02x", payload[i]);
    puts("\"}");
    uint64_t seen[64]; unsigned count = 0;
    for (unsigned off = 0; off + 8 <= length; off += 8) {
        uint64_t candidate; memcpy(&candidate, payload + off, 8);
        for (unsigned hop = 0; hop < 2 && is_game_heap_ptr(candidate); hop++) {
            int32_t index = read_i32(candidate + 0x24);
            if (index > 0 && index < object_count && object_at(index) == candidate) {
                int duplicate = 0; for (unsigned k = 0; k < count; k++) if (seen[k] == candidate) duplicate = 1;
                if (!duplicate && count < 64) {
                    seen[count++] = candidate;
                    uint64_t cls = read_ptr(candidate + OFF_UOBJECT_CLASS); char name[128] = {0}, escaped[256];
                    if (resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, sizeof(name))) {
                        json_escape(name, escaped, sizeof(escaped));
                        printf("{\"record\":\"userdataObject\",\"payloadOffset\":%u,\"indirections\":%u,\"object\":\"%lx\",\"class\":\"%s\",\"registryIndex\":%d}\n",
                               off, hop, candidate, escaped, index);
                    }
                }
                break;
            }
            candidate = read_ptr(candidate);
        }
    }
    return 1;
}

static int native_ui_methods(void) {
    uint64_t fn = object_at(0x326b); char name[128] = {0};
    if (!fn || !resolve_fname(read_i32(fn + OFF_UOBJECT_NAME), name, sizeof(name)) || strcmp(name, "GetCachedGeometry")) return 0;
    uint64_t owner = read_ptr(fn + OFF_UOBJECT_OUTER), method = read_ptr(fn + 0xd8);
    if (!resolve_fname(read_i32(owner + OFF_UOBJECT_NAME), name, sizeof(name)) || strcmp(name, "Widget") ||
        method < ue4_base || method >= ue4_end || !(read_i32(fn + 0xb8) & 0x400)) return 0;
    printf("{\"record\":\"uiMethod\",\"name\":\"Widget.GetCachedGeometry\",\"moduleOffset\":\"%lx\"}\n", method - ue4_base);
    return 1;
}

/* The verified GetCachedGeometry wrapper only reads Widget's cached Slate
 * pointer (+138/140, or weak +128/130) and copies 0x48 bytes at Slate+f0.
 * This diagnostic mirrors those reads without calling it or touching refs. */
static int copy_widget_geometry(uint64_t obj, float out[18], char name[128], int *copied_slot) {
    int32_t index = read_i32(obj + 0x24);
    if (index <= 0 || object_at(index) != obj) return 0;
    uint64_t fn = object_at(0x326b), expected = read_ptr(fn + OFF_UOBJECT_OUTER);
    if (!fn || read_ptr(fn + 0xd8) != ue4_base + 0x1a9cfb3c) return 0;
    if (!resolve_fname(read_i32(expected + OFF_UOBJECT_NAME), name, 128) || strcmp(name, "Widget")) return 0;
    uint64_t cls = read_ptr(obj + OFF_UOBJECT_CLASS), cur = cls;
    for (unsigned depth = 0; depth < 16 && cur && cur != expected; depth++) cur = read_ptr(cur + 0x40);
    if (cur != expected) return 0;
    uint64_t slate = read_ptr(obj + 0x138), control = read_ptr(obj + 0x140);
    int slot = 0x138;
    if (!is_game_heap_ptr(slate) || !is_game_heap_ptr(control) || read_i32(control + 8) < 1) {
        slate = read_ptr(obj + 0x128); control = read_ptr(obj + 0x130); slot = 0x128;
    }
    if (!is_game_heap_ptr(slate) || !is_game_heap_ptr(control) || read_i32(control + 8) < 1) return 0;
    unsigned char bytes[0x48], after[0x48];
    if (read_mem(slate + 0xf0, bytes, sizeof(bytes)) != sizeof(bytes) ||
        read_mem(slate + 0xf0, after, sizeof(after)) != sizeof(after) || memcmp(bytes, after, sizeof(bytes)) ||
        read_ptr(obj + slot) != slate || read_ptr(obj + slot + 8) != control ||
        read_i32(control + 8) < 1 || object_at(index) != obj) return 0;
    if (!resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, 128)) return 0;
    memcpy(out, bytes, sizeof(bytes)); *copied_slot = slot; return 1;
}

static int widget_geometry(uint64_t obj) {
    float floats[18]; char name[128] = {0}, escaped[256]; int slot;
    if (!copy_widget_geometry(obj, floats, name, &slot)) return 0;
    json_escape(name, escaped, sizeof(escaped));
    printf("{\"record\":\"widgetGeometry\",\"class\":\"%s\",\"slot\":%d,\"floats\":[", escaped, slot);
    for (unsigned i = 0; i < 18; i++) {
        float f = floats[i]; if (i) printf(",");
        if (isfinite(f)) printf("%.9g", f); else printf("null");
    }
    puts("]}"); return 1;
}

/* Discover bounded candidates only. Cached widgets can survive after closing;
 * neither registry membership nor geometry establishes current visibility. */
static int ui_objects(const char *filter) {
    if (!filter || strlen(filter) < 4 || strlen(filter) > 96) return 0;
    unsigned matches = 0; int limit = object_count < 300000 ? object_count : 300000;
    for (int i = 1; i < limit; i++) {
        uint64_t obj = object_at(i);
        if (!obj) continue;
        uint64_t cls = read_ptr(obj + OFF_UOBJECT_CLASS);
        char name[128] = {0}, escaped[256];
        if (!obj || !resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, sizeof(name)) ||
            !contains_case_insensitive(name, filter)) continue;
        if (matches == 512) {
            puts("{\"record\":\"uiObjectsEnd\",\"complete\":false,\"limitReached\":true}"); return 0;
        }
        json_escape(name, escaped, sizeof(escaped));
        printf("{\"record\":\"uiObject\",\"object\":\"%lx\",\"class\":\"%s\",\"registryIndex\":%d}\n",
               obj, escaped, i);
        matches++;
    }
    printf("{\"record\":\"uiObjectsEnd\",\"complete\":%s,\"matches\":%u,\"scanned\":%d}\n",
           limit == object_count ? "true" : "false", matches, limit);
    return limit == object_count;
}

#include "pickup_context.h"
#include "pickup_ui_link.h"
#include "pickup_ui.h"

int main(int argc, char **argv) {
    alarm(8);
    if (argc < 2 || argc > 67 || !init_probe()) return 2;
    loot_values.base = ue4_base; loot_values.read = read_loot_value_memory; lv_begin_frame(&loot_values);
    if (!strcmp(argv[1], "schema")) {
        const int indices[] = {0x1e8f, 0x1e90, 0x1e91, 0x1d87,
                               0x4703, 0x4710, 0x4742, 0x7088, 0x6fb3, 0x286, 6322};
        for (unsigned i = 0; i < sizeof(indices)/sizeof(indices[0]); i++) {
            uint64_t obj = object_at(indices[i]);
            if (obj) print_schema(obj);
        }
    } else if (!strcmp(argv[1], "tables")) {
        tables();
    } else if (!strcmp(argv[1], "pickup-context") && argc == 2) {
        printf("{\"record\":\"pickupContextBegin\",\"schemaVersion\":1,\"gamePid\":%d,\"capturedElapsedMs\":%lu}\n",
               game_pid, diag_now_ms());
        lv_refresh(&loot_values, diag_now_ms());
        if (!pickup_context()) return 3;
        printf("{\"record\":\"pickupContextEnd\",\"gamePid\":%d,\"capturedElapsedMs\":%lu}\n",
               game_pid, diag_now_ms());
    } else if (!strcmp(argv[1], "pickup-ui") && argc == 2) {
        if (!pickup_ui()) return 3;
    } else if (!strcmp(argv[1], "inventory-cache") && argc == 2) {
        if (!inventory_cache()) return 3;
    } else if (!strcmp(argv[1], "lua-price-metadata")) {
        lua_price_metadata();
    } else if (!strcmp(argv[1], "lua-function") && argc == 3) {
        char *end = NULL; uint64_t closure = strtoull(argv[2], &end, 16);
        if (!end || *end || !lua_function(closure)) return 3;
    } else if (!strcmp(argv[1], "lua-userdata") && argc == 3) {
        char *end = NULL; uint64_t address = strtoull(argv[2], &end, 16);
        if (!end || end == argv[2] || *end || !lua_userdata(address)) return 3;
    } else if (!strcmp(argv[1], "native-ui-methods") && argc == 2) {
        if (!native_ui_methods()) return 3;
    } else if (!strcmp(argv[1], "widget-geometry") && argc == 3) {
        char *end = NULL; uint64_t obj = strtoull(argv[2], &end, 16);
        if (!end || end == argv[2] || *end || !is_game_heap_ptr(obj) || !widget_geometry(obj)) return 3;
    } else if (!strcmp(argv[1], "ui-objects") && argc == 3) {
        if (!ui_objects(argv[2])) return 3;
    } else if ((!strcmp(argv[1], "lua-table") || !strcmp(argv[1], "lua-get")) && argc >= 3) {
        char *end = NULL;
        uint64_t table = strtoull(argv[2], &end, 16);
        if (!end || *end || !lua_table(table, argc >= 4 ? argv[3] : NULL,
                                       !strcmp(argv[1], "lua-get"))) return 3;
    } else if (!strcmp(argv[1], "price-dispatch")) {
        const uint64_t offsets[] = {0x203feb74, 0x204069ad, 0x204069ae,
                                     0x20517748, 0x20517b8a, 0x20517b8b,
                                     0x20517bf0, 0x20517bee};
        for (unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); i++) {
            unsigned char value = 0;
            if (read_mem(ue4_base + offsets[i], &value, 1) == 1)
                printf("price-dispatch offset=%lx value=%u\n", offsets[i], value);
        }
        char name[128] = {0};
        if (resolve_fname(read_i32(ue4_base + 0x20507ab0), name, sizeof(name)))
            printf("price-table-key name=%s\n", name);
    } else if (!strcmp(argv[1], "pickup-methods")) {
        const int indices[] = {7824, 7825};
        for (unsigned i = 0; i < sizeof(indices) / sizeof(indices[0]); i++) {
            uint64_t cls = object_at(indices[i]), parent = read_ptr(cls + 0x40);
            uint64_t cdo = read_ptr(cls + 0x138), vtable = read_ptr(cdo);
            if (!cls || read_i32(parent + 0x24) != indices[i] - 1 ||
                !is_game_heap_ptr(cdo) || read_ptr(cdo + OFF_UOBJECT_CLASS) != cls ||
                vtable < ue4_base || vtable >= ue4_end) return 3;
            uint64_t method = read_ptr(vtable + 0xc28);
            if (method < ue4_base || method >= ue4_end) return 3;
            printf("{\"nativeClassIndex\":%d,\"getSubPickupsModuleOffset\":\"%lx\"}\n",
                   indices[i], method - ue4_base);
        }
    } else if (!strcmp(argv[1], "inventory-methods")) {
        uint64_t cls = object_at(0x1d35), cdo = read_ptr(cls + 0x138);
        uint64_t vtable = read_ptr(cdo);
        if (!cls || !is_game_heap_ptr(cdo) || read_ptr(cdo + 8) != cls ||
            vtable < ue4_base || vtable >= ue4_end) return 3;
        for (int off = 0x7c0; off <= 0x7c8; off += 8) {
            uint64_t method = read_ptr(vtable + off);
            if (method >= ue4_base && method < ue4_end)
                printf("inventory-method slot=%x moduleOffset=%lx\n", off, method - ue4_base);
        }
    } else if (!strcmp(argv[1], "rows") && argc >= 4) {
        char *end = NULL;
        uint64_t table = strtoull(argv[2], &end, 16);
        if (!end || *end || !is_game_heap_ptr(table) || !rows(table, argc - 3, argv + 3)) return 3;
    } else if (!strcmp(argv[1], "actors")) {
        for (int i = 2; i < argc; i++) {
            char *end = NULL;
            uint64_t obj = strtoull(argv[i], &end, 16);
            if (!end || *end || !is_game_heap_ptr(obj)) return 3;
            actor(obj);
        }
    } else if (!strcmp(argv[1], "nearby-loot") && (argc >= 6 && argc <= 8)) {
        float values[4];
        for (int i = 0; i < 4; i++) {
            char *end = NULL; values[i] = strtof(argv[i + 2], &end);
            if (!end || end == argv[i + 2] || *end || !isfinite(values[i])) return 3;
        }
        if (fabsf(values[0]) > 100000 || fabsf(values[1]) > 100000 ||
            fabsf(values[2]) > 100000 || values[3] <= 0 || values[3] > 1000 ||
            (argc >= 7 && (!argv[6][0] || strlen(argv[6]) > 64))) return 3;
        uint64_t anchor = 0;
        if (argc == 8) {
            char *end = NULL; anchor = strtoull(argv[7], &end, 16);
            if (!end || end == argv[7] || *end || !is_game_heap_ptr(anchor)) return 3;
        }
        if (!nearby_loot(values[0], values[1], values[2], values[3], argc >= 7 ? argv[6] : NULL, anchor)) return 3;
    } else return 3;
    return ferror(stdout) ? 4 : 0;
}
