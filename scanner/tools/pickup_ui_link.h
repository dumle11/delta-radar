/* Pure copied-table linkage for read-only UI research. A matching widget is
 * not yet a verified visible/touchable control. Included after pickup_context.h. */
#ifndef DELTA_PICKUP_UI_LINK_H
#define DELTA_PICKUP_UI_LINK_H
typedef struct {
    uint64_t gid, catalog, widget;
    int count, columns, rows, search_state, raw_show, enabled, dragging;
} PUICell;

static int pui_bool(const PCTable *table, const char *name, int *out) {
    LVValue v = pc_field(table, name);
    if (v.tag != 1) return 0;
    *out = (v.value & 255) != 0; return 1;
}

/* Only the observed 24-byte ARM64 wrapper is accepted. Registry/class and
 * geometry validation must still be performed by the native caller. */
static uint64_t pui_widget_wrapper(LVValue wrapper) {
    unsigned char before[64], after[64]; uint64_t length, object;
    if (wrapper.tag != 71 || !lv_read(&loot_values, wrapper.value, before, sizeof(before)) || before[8] != 7) return 0;
    memcpy(&length, before + 24, 8); memcpy(&object, before + 56, 8);
    if (length != 24 || !lv_pointer(object) ||
        !lv_read(&loot_values, wrapper.value, after, sizeof(after)) || after[8] != 7 ||
        memcmp(before + 10, after + 10, 54)) return 0;
    return object;
}

/* _allItemViews is keyed by ItemBase tables, with item-view table values.
 * Verify both directions and the inventory slot before exposing the GID. */
static int pui_cell_link(PCContext *ctx, const PCEntry *entry, int64_t group, int64_t slot, PUICell *out) {
    if (entry->key_tag != 69 || entry->value.tag != 69) { ctx->complete = 0; return 0; }
    PCTable item = pc_load(ctx, entry->key), view = pc_load(ctx, entry->value.value), place = {0};
    int ok = item.valid && view.valid, destroyed = 1;
    LVValue back = pc_field(&view, "item"), in_slot = pc_field(&item, "InSlot");
    int64_t gid = 0, catalog = 0, count = 0, width = 0, length = 0, actual_group = -1, actual_slot = -1, search = -1;
    if (in_slot.tag == 69) place = pc_load(ctx, in_slot.value);
    ok = ok && back.tag == 69 && back.value == entry->key && place.valid &&
        pc_integer(pc_field(&item, "gid"), &gid) && gid != 0 &&
        pc_integer(pc_field(&item, "id"), &catalog) && catalog > 0 &&
        pc_integer(pc_field(&item, "num"), &count) && count > 0 && count <= 100000 &&
        pc_integer(pc_field(&item, "width"), &width) && width > 0 && width <= 16 &&
        pc_integer(pc_field(&item, "length"), &length) && length > 0 && length <= 16 &&
        pc_integer(pc_field(&place, "_slotGroupId"), &actual_group) && actual_group == group &&
        pc_integer(pc_field(&place, "SlotType"), &actual_slot) && actual_slot == slot &&
        pui_bool(&view, "_has_destroy_", &destroyed) && !destroyed &&
        pui_bool(&view, "_is_enable_", &out->enabled) &&
        pui_bool(&view, "bIsShow", &out->raw_show) &&
        pui_bool(&view, "_bDraging", &out->dragging) &&
        pc_integer(pc_field(&view, "_viewSearchState"), &search) && search >= 0 && search <= 256;
    uint64_t widget = ok ? pui_widget_wrapper(pc_field(&view, "__cppinst")) : 0;
    if (!widget) ok = 0;
    if (ok) {
        out->gid = (uint64_t)gid; out->catalog = (uint64_t)catalog; out->widget = widget;
        out->count = (int)count; out->columns = (int)length; out->rows = (int)width; out->search_state = (int)search;
    } else ctx->complete = 0;
    pc_free(&item); pc_free(&view); pc_free(&place); return ok;
}
#endif
