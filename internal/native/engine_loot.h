#ifndef INTERNAL_ENGINE_LOOT_H
#define INTERNAL_ENGINE_LOOT_H
/* Read-only item catalog: UDataTable.RowMap with SDK FDFMCommonItemRow and
 * FDFMColorDefineRow. Item text uses the signature-checked native FText accessor; no gameplay calls or item writes. */
#define EL_TABLE_MAX 32
#define EL_ROW_SLOTS 65536
typedef struct { EFRef object; uint64_t data; int32_t count, capacity, cursor; int colors; } ELTable;
typedef struct { uint64_t key, row, map_data, numeric; uint32_t index; uint16_t table; } ELRow;
static ELTable el_tables[EL_TABLE_MAX];
static ELRow el_rows[EL_ROW_SLOTS];
static ELRow *el_numeric_rows[EL_ROW_SLOTS];
static int el_table_count, el_object_cursor, el_object_count, el_scan_delay;
static uint64_t el_table_class;
static unsigned el_tick_count, el_row_count;
static uint32_t el_colors[8];
static uint8_t el_color_known[8];
static uint64_t el_color_class;
static EFRef el_color_manager;
static unsigned el_hash(uint64_t key);
typedef struct {uint64_t key,text_data;int64_t next_attempt;char text[256];} ELName;
static ELName el_names[1024];
static unsigned el_text_budget;
static int el_read_text(uint64_t text_address,char *out,size_t size,uint64_t key) {
    uint64_t text_data=ev_ptr(text_address);
    ELName *cache=&el_names[el_hash(key)%1024];
    if(cache->key==key && cache->text_data==text_data && cache->text[0]) {
        snprintf(out,size,"%s",cache->text);return 1;
    }
    if(read_fstring(text_data+0x48,out,(int)size)>0) return 1;
#if defined(__ANDROID__) && defined(__aarch64__)
    if(cache->key==key && cache->text_data==text_data && ef_now()<cache->next_attempt) return 0;
    /* The exact native FText::ToString used by Conv_TextToString returns a
     * borrowed FString reference. It resolves string-table histories under
     * the engine's text locks; no guessed FTextData layout or owned result. */
    uint32_t code[4],expected[]={0xd10183ff,0xa9044ff4,0xa9057bfd,0x910143fd};
    uint64_t text[3],vtable=ev_ptr(text_data);
    uint64_t history=ev_ptr(vtable+0x38);
    uint32_t getter[2]={0};int history_ok=history==ue4_base+0x111a9c78 || history==ue4_base+0x1132331c;
    if(!history_ok && history>=ue4_base && history<ue4_end && ev_read(history,getter,sizeof(getter)))
        history_ok=getter[1]==0xd65f03c0 && (getter[0]==0x91002000 || getter[0]==0x91006000);
    if(el_text_budget && (cache->key!=key || cache->text_data!=text_data || ef_now()>=cache->next_attempt) && camera_frame_valid && local_player_controlled &&
        vtable>=ue4_base && vtable<ue4_end &&
        history_ok &&
        ev_read(text_address,text,sizeof(text)) && text[0]==text_data &&
        ev_read(ue4_base+0x111402a0,code,sizeof(code)) && !memcmp(code,expected,sizeof(code))) {
        --el_text_budget;
        cache->key=key;cache->text_data=text_data;cache->next_attempt=ef_now()+3000;cache->text[0]=0;
        typedef uint64_t (*ToString)(const void *);
        uint64_t string=((ToString)(uintptr_t)(ue4_base+0x111402a0))(text);
        if(read_fstring(string,out,(int)size)>0 && !strstr(out,"MISSING STRING TABLE")) {
            cache->key=key;cache->text_data=text_data;snprintf(cache->text,sizeof(cache->text),"%s",out);
            static unsigned logged;if(logged++<5) {char d[320];snprintf(d,sizeof(d),"loot native name: %s",out);_diag(d);}
            return 1;
        }
    }
#endif
    return 0;
}

static unsigned el_hash(uint64_t key) {
    key ^= key >> 33; key *= UINT64_C(0xff51afd7ed558ccd); key ^= key >> 33;
    return (unsigned)key & (EL_ROW_SLOTS - 1);
}
static ELRow *el_find(uint64_t key, int insert) {
    if (!key) return NULL;
    unsigned slot = el_hash(key);
    for (unsigned i = 0; i < EL_ROW_SLOTS; ++i, slot = (slot + 1) & (EL_ROW_SLOTS - 1)) {
        ELRow *r = &el_rows[slot];
        if (r->key == key || (insert && !r->key)) return r;
        if (!r->key) return NULL;
    }
    return NULL;
}
static uint64_t el_numeric_key(uint64_t id) {
    if(!id) return 0;
    for(unsigned slot=el_hash(id),i=0;i<EL_ROW_SLOTS;i++,slot=(slot+1)&(EL_ROW_SLOTS-1)) {
        ELRow *row=el_numeric_rows[slot];if(!row) return 0;if(row->numeric==id) return row->key;
    }
    return 0;
}
static void el_index_numeric(ELRow *row) {
    char name[40];resolve_fname((int32_t)row->key,name,sizeof(name));
    if((row->key>>32) || !name[0]) return;
    uint64_t id=0;
    for(unsigned i=0;name[i];i++) {
        if(name[i]<'0' || name[i]>'9' || id>(UINT64_MAX-9)/10) return;
        id=id*10+(unsigned)(name[i]-'0');
    }
    if(!id) return;row->numeric=id;
    for(unsigned slot=el_hash(id),i=0;i<EL_ROW_SLOTS;i++,slot=(slot+1)&(EL_ROW_SLOTS-1))
        if(!el_numeric_rows[slot] || el_numeric_rows[slot]->numeric==id) {el_numeric_rows[slot]=row;return;}
}
static uint64_t el_item_key(uint64_t item_info) {
    uint32_t id[2];
    if(!ev_read(item_info+0x10,id,sizeof(id))) return 0;
    /* Matching ItemIDUtil.ToFName: Category * 10000 + Sequence. */
    return el_numeric_key((uint64_t)id[0]*10000+id[1]);
}
/* SDK TMap -> TSparseArray allocation bit array, with four inline DWORDs. */
static int el_allocated(uint64_t map, int index) {
    int32_t bits = 0, capacity = 0;
    if (!ev_read(map + 0x28, &bits, 4) || !ev_read(map + 0x2c, &capacity, 4) ||
        index < 0 || index >= bits || bits > 200000 || capacity < bits || capacity > 1000000) return 0;
    uint64_t secondary = ev_ptr(map + 0x20);
    uint64_t data = secondary ? secondary : map + 0x10;
    uint32_t word = 0;
    return ev_read(data + (uint64_t)(index / 32) * 4, &word, 4) && (word & (1u << (index & 31)));
}
static int el_channel(float linear) {
    if (!isfinite(linear)) return -1;
    if (linear < 0) linear = 0; if (linear > 1) linear = 1;
    float srgb = linear <= 0.0031308f ? linear * 12.92f : 1.055f * powf(linear, 1.0f/2.4f) - 0.055f;
    return (int)lroundf(srgb * 255);
}
static int el_color_at(uint64_t address, uint32_t *out) {
    float rgba[4];
    if (!ev_read(address, rgba, sizeof(rgba))) return 0;
    int r = el_channel(rgba[0]), g = el_channel(rgba[1]), b = el_channel(rgba[2]);
    if (r < 0 || g < 0 || b < 0 || !isfinite(rgba[3]) || rgba[3] < 0 || rgba[3] > 1) return 0;
    *out = 0xff000000u | ((unsigned)r << 16) | ((unsigned)g << 8) | (unsigned)b;
    return 1;
}
static int el_read_color(uint64_t row,uint32_t *out) {return el_color_at(row+0x10,out);}
typedef struct {EFRef object;uint32_t offset;} ELPaletteWidget;
static ELPaletteWidget el_palette_widgets[32];
static unsigned el_palette_widget_count;
static void el_consider_palette_widget(uint64_t object) {
    int complete=1;for(unsigned q=1;q<=6;q++) complete&=el_color_known[q]!=0;
    if(complete) return;
    static struct {uint64_t cls;uint32_t offset;} classes[256];
    uint64_t cls=ev_ptr(object+8); if(!cls) return;
    unsigned slot=(unsigned)(cls>>4)&255u;
    if(classes[slot].cls!=cls) {
        classes[slot].cls=cls;classes[slot].offset=0;
        for(uint64_t current=cls,n=0;current && n<32;current=ev_ptr(current+0x40),n++) {
            char name[80];resolve_fname(read_i32(current+OFF_UOBJECT_NAME),name,sizeof(name));
            if(!strcmp(name,"DFMInteractorButton")) {classes[slot].offset=0x560;break;}
            if(!strcmp(name,"SOLBigMapHUDView_KeyBtn")) {classes[slot].offset=0x348;break;}
            if(!strcmp(name,"DFHDInteractorNameNumItem")) {classes[slot].offset=0x80000330u;break;}
        }
    }
    unsigned format=classes[slot].offset,off=format&0x7fffffffu;
    if(!off || read_i32(cls+0x3c)<(int)off+(format>>31?0x10:0x50)) return;
    int32_t count=read_i32(object+off+8);if(count<1 || count>16 || ((format>>31) && count<7)) return;
    for(unsigned i=0;i<el_palette_widget_count;i++) if(el_palette_widgets[i].object.object.address==object) return;
    ELPaletteWidget candidate={.offset=format};
    if(!ef_ref(object,&candidate.object)) return;
    for(unsigned i=0;i<el_palette_widget_count;i++) if(!ef_same_function(&el_palette_widgets[i].object)) {el_palette_widgets[i]=candidate;return;}
    if(el_palette_widget_count<32) el_palette_widgets[el_palette_widget_count++]=candidate;
}
static void el_load_widget_palettes(void) {
    if(el_tick_count%60) return;
    for(unsigned w=0;w<el_palette_widget_count;w++) {
        ELPaletteWidget *widget=&el_palette_widgets[w];if(!ef_same_function(&widget->object)) continue;
        uint64_t map=widget->object.object.address+(widget->offset&0x7fffffffu);EVArray list={0};
        if(!ev_read(map,&list,sizeof(list)) || !is_any_ptr(list.data) || list.count<1 || list.count>16 || list.capacity<list.count || list.capacity>64) continue;
        if(widget->offset>>31) {
            if(list.count<7 || list.count>8) continue;
            for(int q=1;q<=6;q++) if(el_color_at(list.data+(uint64_t)q*16,&el_colors[q])) el_color_known[q]=1;
            continue;
        }
        for(int i=0;i<list.count;i++) {
            uint64_t entry=list.data+(uint64_t)i*28;int32_t quality=0;
            if(el_allocated(map,i) && ev_read(entry,&quality,4) && quality>=1 && quality<=7 &&
                el_color_at(entry+4,&el_colors[quality])) el_color_known[quality]=1;
        }
    }
}
static int el_table_shape(uint64_t object, uint64_t *data, int32_t *count, int32_t *capacity) {
    return ev_read(object + 0x30, data, 8) && ev_read(object + 0x38, count, 4) &&
        ev_read(object + 0x3c, capacity, 4) && is_any_ptr(*data) && *count >= 0 &&
        *count <= 200000 && *capacity >= *count && *capacity <= 1000000;
}
static void el_consider_table(uint64_t object) {
    if (!el_table_class || ev_ptr(object + 8) != el_table_class) return;
    for (int i = 0; i < el_table_count; ++i) if (el_tables[i].object.object.address == object) return;
    uint64_t row_struct = ev_ptr(object + 0x28);
    char name[80]; resolve_fname(read_i32(row_struct + OFF_UOBJECT_NAME), name, sizeof(name));
    int colors = !strcmp(name, "DFMColorDefineRow") || !strcmp(name, "TextColorDefineRow");
    if ((!colors && strcmp(name, "DFMCommonItemRow")) || el_table_count >= EL_TABLE_MAX) return;
    ELTable t = {.colors = colors};
    if (!ef_ref(object, &t.object) || !el_table_shape(object, &t.data, &t.count, &t.capacity)) return;
    el_tables[el_table_count++] = t;
    char text[160]; snprintf(text, sizeof(text), "loot table: %s object=%lx entries=%d", name, (unsigned long)object, t.count); _diag(text);
}
static void el_consider_color_manager(uint64_t object) {
    if (!el_color_class) {
        char name[64],type[40];
        resolve_fname(read_i32(object+OFF_UOBJECT_NAME),name,sizeof(name));
        if (!strcmp(name,"GPColorManager")) {
            resolve_fname(read_i32(ev_ptr(object+8)+OFF_UOBJECT_NAME),type,sizeof(type));
            if(!strcmp(type,"Class")) el_color_class=object;
        }
    }
    if (el_color_class && ev_ptr(object+8)==el_color_class && !(read_i32(object+0x18)&0x10)) {
        ef_ref(object,&el_color_manager);
        static int reported;if(!reported) {char d[100];snprintf(d,sizeof(d),"loot color manager: object=%lx rows=%d",(unsigned long)object,read_i32(object+0x38));_diag(d);reported=1;}
    }
}
static void el_load_palette(void) {
    if (el_tick_count%60 || !ef_same_function(&el_color_manager)) return;
    /* SDK GPColorManager.CachedLinerColorMap: FString/FLinearColor pairs,
     * followed by two hash indices (40-byte sparse elements). The game uses
     * A00<quality>_<alpha> as the cached key for GetColorByQualityId. */
    uint64_t map=el_color_manager.object.address+0x30;
    EVArray array={0};
    if (!ev_read(map,&array,sizeof(array)) || !is_any_ptr(array.data) ||
        array.count<0 || array.count>4096 || array.capacity<array.count || array.capacity>8192) return;
    static int palette_logged;
    int log_palette=!palette_logged && array.count>0;
    if(log_palette) {char d[120];snprintf(d,sizeof(d),"loot palette: manager=%lx rows=%d",(unsigned long)el_color_manager.object.address,array.count);_diag(d);palette_logged=1;}
    for(int i=0;i<array.count;i++) {
        if (!el_allocated(map,i)) continue;
        uint64_t entry=array.data+(uint64_t)i*40; char key[96];
        int key_len=read_fstring(entry,key,sizeof(key));
        if(log_palette && i<20) {char d[180];snprintf(d,sizeof(d),"loot palette key: %s",key);_diag(d);}
        if (key_len<4 || strncmp(key,"A00",3) ||
            key[3]<'1' || key[3]>'7' || (key[4] && key[4]!='_')) continue;
        unsigned q=(unsigned)(key[3]-'0');
        if (el_read_color(entry,&el_colors[q])) el_color_known[q]=1;
    }
}
static void el_discover(void) {
    uint64_t item = 0;
    if (!el_table_class && ev_item(0x289, &item)) {
        uint64_t cls = ev_ptr(item); char name[40];
        resolve_fname(read_i32(cls + OFF_UOBJECT_NAME), name, sizeof(name));
        if (!strcmp(name, "DataTable")) el_table_class = cls;
    }
    if (!el_table_class) return;
    if (!el_color_class && ev_item(0xe38,&item)) {
        uint64_t cls=ev_ptr(item); char name[40];resolve_fname(read_i32(cls+OFF_UOBJECT_NAME),name,sizeof(name));
        if (!strcmp(name,"GPColorManager")) el_color_class=cls;
    }
    if (el_tick_count == 1) {
        if (ev_item(0x1b903, &item)) el_consider_table(ev_ptr(item));
        if (ev_item(0x19af7, &item)) el_consider_table(ev_ptr(item));
    }
    int32_t count = 0;
    if (!ev_read(ue4_base + OFF_GUOBJECTARRAY_FROM_BASE + 0x14, &count, 4) || count < 1 || count > 2000000) return;
    if (el_object_cursor >= count) {
        if (count == el_object_count && ++el_scan_delay < 300) return;
        el_object_cursor = 0; el_scan_delay = 0;
    }
    el_object_count = count;
    for (int n = 0; n < 192 && el_object_cursor < count; ++n, ++el_object_cursor)
        if (ev_item(el_object_cursor, &item)) {
            uint64_t object=ev_ptr(item); el_consider_table(object); el_consider_color_manager(object); el_consider_palette_widget(object);
        }
}
static void el_load_rows(void) {
    int budget = 256;
    for (int t = 0; t < el_table_count && budget; ++t) {
        ELTable *table = &el_tables[t];
        if (!ef_same_function(&table->object)) continue;
        uint64_t data; int32_t count, capacity;
        if (!el_table_shape(table->object.object.address, &data, &count, &capacity)) continue;
        if (data != table->data || count != table->count) { table->data = data; table->count = count; table->cursor = 0; }
        for (; table->cursor < count && budget; ++table->cursor, --budget) {
            int i = table->cursor;
            if (!el_allocated(table->object.object.address + 0x30, i)) continue;
            struct { uint64_t key, row; } entry;
            if (!ev_read(data + (uint64_t)i * 24, &entry, sizeof(entry)) || !is_any_ptr(entry.row)) continue;
            if (table->colors) {
                char name[40]; resolve_fname((int32_t)entry.key, name, sizeof(name));
                char diagnostic[160]; snprintf(diagnostic,sizeof(diagnostic),"loot color row: name=%s number=%u row=%lx",name,(unsigned)(entry.key>>32),(unsigned long)entry.row); _diag(diagnostic);
                /* GetColorByQualityId formats the game's row key as A00%d. */
                if (strlen(name) == 4 && !strncmp(name, "A00", 3) && name[3] >= '1' && name[3] <= '7') {
                    int quality = name[3] - '0';
                    if (el_read_color(entry.row, &el_colors[quality])) el_color_known[quality] = 1;
                }
            } else {
                uint64_t row_key = 0;
                if (!ev_read(entry.row + 0x10, &row_key, 8) || row_key != entry.key) continue;
                ELRow *r = el_find(entry.key, 1);
                if (r) { if (!r->key) ++el_row_count; r->key = entry.key; r->row = entry.row; r->map_data = data; r->index = (uint32_t)i; r->table = (uint16_t)t; el_index_numeric(r); }
            }
        }
    }
}
static int el_metadata(uint64_t key, char *name, size_t name_size, int *quality, uint64_t *value, int *value_known) {
    ELRow *r = el_find(key, 0);
    if (!r || r->table >= el_table_count || !ef_same_function(&el_tables[r->table].object)) return 0;
    ELTable *table = &el_tables[r->table];
    struct { uint64_t key, row; } current;
    if (ev_ptr(table->object.object.address + 0x30) != r->map_data ||
        !el_allocated(table->object.object.address + 0x30, (int)r->index) ||
        !ev_read(r->map_data + (uint64_t)r->index * 24, &current, sizeof(current)) ||
        current.key != key || current.row != r->row) return 0;
    uint64_t row_key = 0; int32_t q = -1, price = -1;
    if (!ev_read(r->row + 0x10, &row_key, 8) || row_key != key ||
        !ev_read(r->row + 0x68, &q, 4) || q < 0 || q > 7) return 0;
    if(!el_read_text(r->row+0x18,name,name_size,key) &&
        !el_read_text(r->row+0x50,name,name_size,key^UINT64_C(0x917b6a03d42f8001))) snprintf(name,name_size,"Unknown item");
    *value_known = ev_read(r->row + 0xdc, &price, 4) && price > 0;
    *quality = q; *value = *value_known ? (uint32_t)price : 0;
    return 1;
}
static int el_append_contents(const Entity *parent,int count) {
    if(count>=MAX_ACTORS || !ew_is_a(parent->actor_address,"InventoryPickup_Container",0x22a0)) return count;
    EVArray items={0},after={0};uint64_t array=parent->actor_address+0x2290;
    if(!ev_read(array,&items,sizeof(items)) || !is_any_ptr(items.data) || items.count<1 || items.count>128 || items.capacity<items.count || items.capacity>512) return count;
    int first=count;
    for(int i=0;i<items.count && count<MAX_ACTORS;i++) {
        uint64_t info=items.data+(uint64_t)i*0x900,key=el_item_key(info);
        if(!key) continue;
        Entity item={0};item.loot_stack=read_i32(info+0x38);
        if(item.loot_stack<1 || item.loot_stack>1000000 ||
            !el_metadata(key,item.loot_name,sizeof(item.loot_name),&item.loot_quality,&item.loot_unit_value,&item.loot_value_known) ||
            key!=el_item_key(info)) continue;
        item.loot_kind=LOOT_ITEM;item.loot_in_container=1;item.loot_line=count-first+1;item.loot_metadata_known=1;
        item.loot_color_known=el_color_known[item.loot_quality];item.loot_color=el_colors[item.loot_quality];
        snprintf(item.id,sizeof(item.id),"li_%llx_%d",(unsigned long long)parent->actor_address,i);snprintf(item.kind,sizeof(item.kind),"l");
        item.actor_address=parent->actor_address;item.x=parent->x;item.y=parent->y;item.z=parent->z;item.half_height=.1f;
        entities[count++]=item;
    }
    if(!ev_read(array,&after,sizeof(after)) || memcmp(&items,&after,sizeof(items))) return first;
    return count;
}
static int el_update(int count) {
    ++el_tick_count; el_text_budget=1; el_discover(); el_load_rows(); el_load_palette(); el_load_widget_palettes();
    int loot = 0, resolved = 0;
    for (int i = 0; i < count; ++i) {
        Entity *e = &entities[i]; if (e->kind[0] != 'l') continue;
        ++loot;
        e->loot_metadata_known = e->loot_value_known = e->loot_color_known = 0; e->loot_quality = 0; e->loot_unit_value = 0;
        e->loot_stack = 1;
        const char *fallback = e->loot_kind == LOOT_DEATH_BOX ? "Death box" :
            e->loot_kind == LOOT_CONTAINER ? "Container" : e->loot_kind == LOOT_STASH ? "Stash" : "Unknown item";
        snprintf(e->loot_name, sizeof(e->loot_name), "%s", fallback);
        if (e->loot_kind != LOOT_ITEM && ew_is_a(e->actor_address,"InteractorBase",0x850)) {
            /* SDK AInteractorBase.InteractorName is the game's container title. */
            char name[256];
            if (el_read_text(e->actor_address+0x838,name,sizeof(name),e->actor_address))
                snprintf(e->loot_name, sizeof(e->loot_name), "%s", name);
        }
        if (e->loot_kind != LOOT_STASH && ew_is_a(e->actor_address,"PickupBase",0x10c4)) {
            uint64_t key = 0; int32_t stack = 1;
            if (ev_read(e->actor_address + 0x10c0, &stack, 4) && stack > 0 && stack <= 1000000) e->loot_stack = stack;
            if(e->loot_kind==LOOT_ITEM && ew_is_a(e->actor_address,"InventoryPickup",0x1340)) key=el_item_key(e->actor_address+0x1300);
            if(!key) ev_read(e->actor_address+0x10b0,&key,8);
            char catalog_name[256];
            if (el_metadata(key,catalog_name,sizeof(catalog_name), &e->loot_quality, &e->loot_unit_value, &e->loot_value_known)) {
                if(strcmp(catalog_name,"Unknown item") || e->loot_kind==LOOT_ITEM) snprintf(e->loot_name,sizeof(e->loot_name),"%s",catalog_name);
                e->loot_metadata_known = 1; resolved+=strcmp(e->loot_name,"Unknown item")!=0;
                e->loot_color_known = el_color_known[e->loot_quality];
                e->loot_color = el_colors[e->loot_quality];
            }
        }
        /* A container isn't an item in its own contents. Its contents below
         * carry their own unit reference values and rarity independently. */
        if(e->loot_kind!=LOOT_ITEM) {e->loot_metadata_known=e->loot_value_known=e->loot_color_known=0;e->loot_quality=0;e->loot_unit_value=0;}
    }
    int published=count;
    for(int i=0;i<count && published<MAX_ACTORS;i++) if(entities[i].kind[0]=='l' &&
        (entities[i].loot_kind==LOOT_CONTAINER || entities[i].loot_kind==LOOT_DEATH_BOX)) published=el_append_contents(&entities[i],published);
    if (el_tick_count == 1 || el_tick_count % 200 == 0) {
        unsigned colors = 0; for (int i = 1; i < 8; ++i) colors += el_color_known[i] != 0;
        char text[180]; snprintf(text, sizeof(text), "loot: tables=%d rows=%u colors=%u scan=%d/%d actors=%d named=%d",
            el_table_count, el_row_count, colors, el_object_cursor, el_object_count, loot, resolved); _diag(text);
    }
    return published;
}
#endif
