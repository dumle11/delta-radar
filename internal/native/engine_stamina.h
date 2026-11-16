#ifndef INTERNAL_ENGINE_STAMINA_H
#define INTERNAL_ENGINE_STAMINA_H
/* Matching SDK: local DFMCharacter -> GPStaminaDataComponent -> GPAttributeSetSpeed.
 * Preserve attribute metadata and current stamina; only consumption rates change. */
static _Atomic int es_enabled;
static _Atomic unsigned es_state;
typedef struct { EVObject object; int32_t serial; } ESRef;
static ESRef es_pawn, es_component, es_speed;
static float es_before[4];
static unsigned es_saved, es_diag_seq;
static const uint32_t es_offsets[4] = {0x150, 0x154, 0x1c0, 0x1c4};
static int es_ref(uint64_t address, ESRef *out) {
    uint64_t item;
    memset(out, 0, sizeof(*out));
    return ev_object(address, &out->object) && ev_item(out->object.index, &item) &&
        ev_read(item + 0x14, &out->serial, 4) && out->serial >= 0;
}
static int es_same(const ESRef *ref) {
    ESRef now;
    return es_ref(ref->object.address, &now) &&
        (!ref->serial || now.serial == ref->serial) && now.object.index == ref->object.index &&
        now.object.cls == ref->object.cls && now.object.name == ref->object.name;
}
static int es_links(void) {
    return es_same(&es_pawn) && es_same(&es_component) && es_same(&es_speed) &&
        ev_ptr(es_pawn.object.address + 0x3ad0) == es_component.object.address &&
        ev_ptr(es_component.object.address + 0x1d8) == es_pawn.object.address &&
        ev_ptr(es_component.object.address + 0x1e0) == es_speed.object.address;
}
static void es_release(void) {
    if (es_saved && es_links()) for (unsigned i = 0; i < 4; i++) {
        float current = NAN;
        if ((es_saved & (1u << i)) && ev_read(es_speed.object.address + es_offsets[i], &current, 4) &&
            current == 0.0f) ew_write(es_speed.object.address + es_offsets[i], &es_before[i], 4);
    }
    es_saved = 0;
    memset(&es_pawn, 0, sizeof(es_pawn)); memset(&es_component, 0, sizeof(es_component));
    memset(&es_speed, 0, sizeof(es_speed)); atomic_store(&es_state, 0);
}
static void es_update(uint64_t pawn) {
    unsigned stage = 0;
    uint64_t component = 0, speed = 0;
    float value = NAN, maximum = NAN, rates[4] = {NAN,NAN,NAN,NAN};
    uint8_t dead = 1;
    int enabled = atomic_load(&es_enabled);
    if (es_saved && (!enabled || es_pawn.object.address != pawn || !es_links())) es_release();
    if (!ew_is_a(pawn, "DFMCharacter", 0x3ad8) ||
        !ev_read(pawn + OFF_CHARACTER_LAST_IS_DEAD, &dead, 1) || (dead & 1)) goto done;
    stage = 1;
    component = ev_ptr(pawn + 0x3ad0);
    if (!ew_is_a(component, "GPStaminaDataComponent", 0x208) || ev_ptr(component + 0x1d8) != pawn) goto done;
    stage = 2;
    speed = ev_ptr(component + 0x1e0);
    if (!ew_is_a(speed, "GPAttributeSetSpeed", 0x1c8)) goto done;
    stage = 3;
    if (!ev_read(speed + 0x124, &value, 4) || !ev_read(speed + 0x134, &maximum, 4) ||
        !isfinite(value) || !isfinite(maximum) || maximum <= 0 || maximum > 1000000 ||
        value < 0 || value > maximum * 1.1f) goto done;
    for (int i = 0; i < 4; i++)
        if (!ev_read(speed + es_offsets[i], &rates[i], 4) || !isfinite(rates[i]) || fabsf(rates[i]) > 100000) goto done;
    stage = 4;
    if (!enabled) goto done;
    if (!es_saved && (!es_ref(pawn, &es_pawn) || !es_ref(component, &es_component) || !es_ref(speed, &es_speed))) goto done;
    if (!es_links()) goto done;
    for (unsigned i = 0; i < 4; i++) {
        float zero = 0;
        if (!(es_saved & (1u << i))) es_before[i] = rates[i];
        if (rates[i] != 0 && !ew_write(speed + es_offsets[i], &zero, 4)) goto done;
        es_saved |= 1u << i;
    }
    stage = 5;
done:
    if (enabled && stage != 5 && es_saved) es_release();
    atomic_store(&es_state, enabled ? (stage == 5 ? 2 : 3) : 0);
    if (++es_diag_seq == 1 || es_diag_seq % 100 == 0) {
        char text[240];
        snprintf(text, sizeof(text), "stamina: enabled=%d state=%u stage=%u pawn=%lx comp=%lx attr=%lx value=%.3f/%.3f sprint=%.3f/%.3f cost=%.3f/%.3f",
            enabled, atomic_load(&es_state), stage, (unsigned long)pawn, (unsigned long)component,
            (unsigned long)speed, value, maximum, rates[0], rates[1], rates[2], rates[3]);
        _diag(text);
    }
}
#endif
