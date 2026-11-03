/* One-shot, bounded loot metadata inspection. No remote calls or writes.
 * Uses reflected properties rather than assuming the dumped actor layout.
 * Output contains item metadata only; no player/account identifiers. */
#define main scanner_main
#include "../memscan.c"
#undef main

typedef struct { uint64_t field, type; int offset, size; } LootProperty;
static uint64_t object_chunks;
static int object_count;

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

static void actor(uint64_t obj) {
    uint64_t cls = read_ptr(obj + OFF_UOBJECT_CLASS);
    int kind = loot_kind_from_class(cls);
    if (!kind) return;
    char class_name[128] = {0}, escaped[256];
    resolve_class_name(cls, class_name, sizeof(class_name));
    json_escape(class_name, escaped, sizeof(escaped));
    printf("{\"actor\":\"%lx\",\"class\":\"%s\",\"kind\":%d", obj, escaped, kind);
    LootProperty p;
    if (property(cls, "InventoryIdName", &p)) {
        char name[128] = {0};
        if (resolve_fname(read_i32(obj + p.offset), name, sizeof(name))) {
            json_escape(name, escaped, sizeof(escaped));
            printf(",\"itemNameId\":\"%s\"", escaped);
        }
    }
    const char *fields[] = {"StackCount", "bIsHiddenItem", "bClientSearchDone",
                           "bIsEmpty", "bFirstOpened"};
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
    printf("}\n");
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
        if (row_index != 0x7085 && row_index != 0x7088) continue;
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

static int rows(uint64_t table, int argc, char **argv) {
    uint64_t cls = read_ptr(table + OFF_UOBJECT_CLASS), row = read_ptr(table + 0x28);
    int row_index = read_i32(row + 0x24);
    if (read_i32(cls + 0x24) != 0x286 ||
        (row_index != 0x7085 && row_index != 0x7088)) return 0;
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
                   row_index == 0x7088 ? "ItemPriceTable" : "GameItem");
            if (row_index == 0x7088) {
                printf(",\"rowItemId\":%lu,\"price\":%u", read_ptr(value + 0x10),
                       (uint32_t)read_i32(value + 0x18));
            } else {
                /* Native price lookup at module+0x1606a3c8 reads this
                 * unreflected slot before falling back to InitialGuidePrice.
                 * Keep it explicitly a candidate until compared with the UI. */
                printf(",\"cachedPriceCandidate\":%d", read_i32(value + 0x23c));
                const char *fields[] = {"Quality", "RecyclePrice", "InitialGuidePrice",
                                         "SellPriceRates", "MaxStackCount"};
                for (unsigned j = 0; j < sizeof(fields)/sizeof(fields[0]); j++)
                    number_field(value, row, fields[j]);
                LootProperty p;
                if (property(row, "Name", &p)) {
                    char text[128] = {0}, escaped_text[256];
                    uint64_t text_data = read_ptr(value + p.offset);
                    if (is_any_ptr(text_data) && read_fstring(text_data + 0x48, text, sizeof(text))) {
                        json_escape(text, escaped_text, sizeof(escaped_text));
                        printf(",\"name\":\"%s\"", escaped_text);
                    }
                }
            }
            printf("}\n"); matches++; break;
        }
    }
    free(entries); free(allocated);
    fprintf(stderr, "rows-matched=%d slots=%d\n", matches, slots);
    return 1;
}

int main(int argc, char **argv) {
    alarm(8);
    if (argc < 2 || argc > 67 || !init_probe()) return 2;
    if (!strcmp(argv[1], "schema")) {
        const int indices[] = {0x1e8f, 0x1e90, 0x1e91, 0x1d87,
                               0x4703, 0x4710, 0x4742, 0x7088, 0x286};
        for (unsigned i = 0; i < sizeof(indices)/sizeof(indices[0]); i++) {
            uint64_t obj = object_at(indices[i]);
            if (obj) print_schema(obj);
        }
    } else if (!strcmp(argv[1], "tables")) {
        tables();
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
    } else return 3;
    return ferror(stdout) ? 4 : 0;
}
