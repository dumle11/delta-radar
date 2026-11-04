/* One-shot live UI acquisition, diagnostic only. No input, Lua calls or writes.
 * Render-space geometry and raw flags are deliberately not action eligibility. */
#ifndef DELTA_PICKUP_UI_H
#define DELTA_PICKUP_UI_H
static int pui_name(const PCTable *t, const char *name) {
    LVValue v = pc_field(t, "_cname");
    return lv_string_equal(&loot_values, v.value, v.tag, name);
}

static uint64_t pui_unwrap(PCContext *ctx, uint64_t address) {
    PCTable t = pc_load(ctx, address); LVValue data = pc_field(&t, "__ins_data__");
    pc_free(&t); return data.tag == 69 ? data.value : address;
}

static void pui_geometry_fields(const float g[18]) {
    /* LocalToAbsolute's verified matrix and translation, not phone pixels. */
    const unsigned fields[] = {0, 1, 9, 10, 11, 12, 13, 14};
    const char *names[] = {"width", "height", "xx", "xy", "yx", "yy", "tx", "ty"};
    for (unsigned i = 0; i < 8; i++) {
        printf(",\"%s\":", names[i]);
        if (isfinite(g[fields[i]])) printf("%.9g", g[fields[i]]); else printf("null");
    }
}

static int pui_geometry(uint64_t object, float g[18]) {
    char name[128] = {0}; int slot;
    if (!copy_widget_geometry(object, g, name, &slot)) return 0;
    const unsigned fields[] = {0, 1, 9, 10, 11, 12, 13, 14};
    for (unsigned i = 0; i < 8; i++) if (!isfinite(g[fields[i]])) return 0;
    return g[0] > 0 && g[1] > 0 && g[0] <= 10000 && g[1] <= 10000;
}

static void pui_slot(PCContext *ctx, uint64_t panel, const char *role, int64_t group, int64_t kind) {
    uint64_t actual = pui_unwrap(ctx, panel);
    PCTable t = pc_load(ctx, actual);
    LVValue bind = pc_field(&t, "bindContainerSlot"), view = pc_field(&t, "_wtContainerSlotView");
    if (!pui_name(&t, "WarehouseContainerPanel") || bind.tag != 69 || view.tag != 69) ctx->complete = 0;
    PCTable bound = bind.tag == 69 ? pc_load(ctx, bind.value) : (PCTable){0};
    int64_t slot = -1, slot_group = -1;
    if (!pc_integer(pc_field(&bound, "SlotType"), &slot) || slot != kind ||
        !pc_integer(pc_field(&bound, "_slotGroupId"), &slot_group) || slot_group != group) ctx->complete = 0;
    uint64_t view_address = view.tag == 69 ? pui_unwrap(ctx, view.value) : 0;
    PCTable views = pc_load(ctx, view_address);
    LVValue item_slot = pc_field(&views, "itemSlot"), all = pc_field(&views, "_allItemViews");
    if (!pui_name(&views, "InvSlotView") || item_slot.tag != 69 || item_slot.value != bind.value || all.tag != 69)
        ctx->complete = 0;
    PCTable cells = all.tag == 69 ? pc_load(ctx, all.value) : (PCTable){0};
    if (!cells.valid || cells.count > PC_MAX_ITEMS) ctx->complete = 0;
    if (pc_emit(ctx)) printf("{\"record\":\"pickupUiSlot\",\"role\":\"%s\",\"group\":%ld,\"slot\":%ld,\"entries\":%u}\n",
                            role, group, kind, cells.count);
    uint64_t seen[PC_MAX_ITEMS]; unsigned seen_count = 0;
    if (ctx->complete) for (unsigned i = 0; i < cells.count; i++) {
        PUICell cell = {0}; float geometry[18];
        if (!pui_cell_link(ctx, &cells.entries[i], group, kind, &cell) || !pui_geometry(cell.widget, geometry)) {
            ctx->complete = 0; break;
        }
        int duplicate = 0;
        for (unsigned j = 0; j < seen_count; j++) if (seen[j] == cell.gid) duplicate = 1;
        if (duplicate) { ctx->complete = 0; break; }
        seen[seen_count++] = cell.gid;
        if (!pc_emit(ctx)) break;
        printf("{\"record\":\"pickupUiCell\",\"role\":\"%s\",\"gid\":\"%016lx\",\"catalogId\":%lu,\"count\":%d,\"columns\":%d,\"rows\":%d,\"widgetToken\":\"%016lx\",\"viewToken\":\"%016lx\",\"rawShow\":%s,\"enabled\":%s,\"dragging\":%s,\"rawSearchState\":%d",
               role, cell.gid, cell.catalog, cell.count, cell.columns, cell.rows, cell.widget, cells.entries[i].value.value,
               cell.raw_show ? "true" : "false", cell.enabled ? "true" : "false",
               cell.dragging ? "true" : "false", cell.search_state);
        pui_geometry_fields(geometry); puts("}");
    }
    pc_free(&cells); pc_free(&views); pc_free(&bound); pc_free(&t);
}

static int pickup_ui(void) {
    PCContext ctx = {.complete = 1}; loot_values.read_budget = 32768;
    uint64_t started = diag_now_ms(), vm = lv_ptr(&loot_values, ue4_base + 0x1fc9cd70);
    uint64_t global = lv_ptr(&loot_values, vm + 24); LVValue registry, roots[2]; unsigned char type = 0;
    if (!lv_read(&loot_values, vm + 8, &type, 1) || type != 8 ||
        !lv_read(&loot_values, global + 0x40, &registry, sizeof(registry)) || registry.tag != 69 ||
        !lv_read(&loot_values, lv_ptr(&loot_values, registry.value + 16), roots, sizeof(roots)) ||
        roots[0].tag != 72 || roots[0].value != vm || roots[1].tag != 69) return 0;
    printf("{\"record\":\"pickupUiBegin\",\"schemaVersion\":1,\"gamePid\":%d,\"capturedElapsedMs\":%lu}\n", game_pid, started);
    PCTable globals = pc_load(&ctx, roots[1].value);
    uint64_t server = pc_table_value(&ctx, &globals, "Server"), facade = pc_table_value(&ctx, &globals, "Facade");
    PCTable groups = pc_load(&ctx, pc_table_value(&ctx, &globals, "ESlotGroup"));
    PCTable kinds = pc_load(&ctx, pc_table_value(&ctx, &globals, "ESlotType"));
    PCTable pickup_types = pc_load(&ctx, pc_table_value(&ctx, &globals, "EGamePickupType"));
    int64_t player = -1, nearby = -1, bag = -1, rig = -1, source = -1, scene = -1, selected_type = -1;
    if (!pc_integer(pc_field(&groups, "Player"), &player) ||
        !pc_integer(pc_field(&groups, "Nearby"), &nearby) || player == nearby ||
        !pc_integer(pc_field(&kinds, "BagContainer"), &bag) ||
        !pc_integer(pc_field(&kinds, "ChestHangingContainer"), &rig) || bag == rig ||
        !pc_integer(pc_field(&kinds, "NearbyContainer"), &source) ||
        !pc_integer(pc_field(&pickup_types, "SceneBox"), &scene) ||
        player < 0 || player > 128 || nearby < 0 || nearby > 128 ||
        bag <= 0 || rig <= 0 || source <= 0 || scene < 0) ctx.complete = 0;
    PCTable loot = pc_load(&ctx, pc_child(&ctx, server, "LootingServer"));
    LVValue selector = pc_field(&loot, "_currentSelectorData");
    int present = selector.tag == 69;
    PCTable selected = present ? pc_load(&ctx, selector.value) : (PCTable){0};
    int supported = present && pc_integer(pc_field(&selected, "type"), &selected_type) && selected_type == scene;
    if (selector.tag != 0 && selector.tag != 69) ctx.complete = 0;
    if (pc_emit(&ctx)) printf("{\"record\":\"pickupUiWindow\",\"selectorPresent\":%s,\"supportedSceneBox\":%s,\"selectorToken\":\"%016lx\"}\n",
                            present ? "true" : "false", supported ? "true" : "false", present ? selector.value : 0);
    if (ctx.complete && supported) {
        uint64_t manager = pc_child(&ctx, facade, "UIManager");
        PCTable root = pc_load(&ctx, pc_child(&ctx, manager, "_viewportPersistentRootUI"));
        if (!pui_name(&root, "RootUI")) ctx.complete = 0;
        float viewport[18]; uint64_t viewport_object = pui_widget_wrapper(pc_field(&root, "_wtCanvasFrameRoot"));
        if (!viewport_object || !pui_geometry(viewport_object, viewport)) ctx.complete = 0;
        else if (pc_emit(&ctx)) { printf("{\"record\":\"pickupUiRootGeometry\""); pui_geometry_fields(viewport); puts("}"); }
        PCTable children = pc_load(&ctx, pc_table_value(&ctx, &root, "_childWidgets"));
        uint64_t bag_view = 0;
        if (children.count > 64) ctx.complete = 0;
        else for (unsigned i = 0; i < children.count; i++) {
            if (children.entries[i].value.tag != 69) { ctx.complete = 0; continue; }
            PCTable child = pc_load(&ctx, children.entries[i].value.value);
            if (pui_name(&child, "MobileBagPanel")) {
                if (bag_view) ctx.complete = 0;
                bag_view = children.entries[i].value.value;
            }
            pc_free(&child);
        }
        if (!bag_view) ctx.complete = 0;
        if (ctx.complete) {
            uint64_t sub = pui_unwrap(&ctx, pc_child(&ctx, bag_view, "_currentSubView"));
            PCTable main = pc_load(&ctx, sub);
            if (!pui_name(&main, "LootingMainView")) ctx.complete = 0;
            if (ctx.complete) {
                uint64_t litter = pc_table_value(&ctx, &main, "_wtLootingLitter");
                pui_slot(&ctx, pc_child(&ctx, litter, "_wtContainerPanel"), "source", nearby, source);
                uint64_t own = pc_table_value(&ctx, &main, "_wtLootingEquipPanelV1");
                pui_slot(&ctx, pc_child(&ctx, own, "_wtBagContainerView"), "backpack", player, bag);
                pui_slot(&ctx, pc_child(&ctx, own, "_wtCHContainerView"), "rig", player, rig);
            }
            pc_free(&main);
        }
        pc_free(&children); pc_free(&root);
    }
    pc_free(&globals); pc_free(&groups); pc_free(&kinds); pc_free(&pickup_types); pc_free(&loot); pc_free(&selected);
    int stable = pc_guards_stable(&ctx);
    if (!stable || lv_ptr(&loot_values, ue4_base + 0x1fc9cd70) != vm) ctx.complete = 0;
    printf("{\"record\":\"pickupUiSummary\",\"complete\":%s,\"actionReady\":false,\"guardsStable\":%s,\"dataRecords\":%u,\"tablesCopied\":%u,\"readsRemaining\":%u,\"gamePid\":%d,\"capturedElapsedMs\":%lu}\n",
           ctx.complete ? "true" : "false", stable ? "true" : "false", ctx.records, ctx.count, loot_values.read_budget, game_pid, diag_now_ms());
    return 1;
}
#endif
