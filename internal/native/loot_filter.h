#ifndef INTERNAL_LOOT_FILTER_H
#define INTERNAL_LOOT_FILTER_H
#include <stdint.h>
#include <math.h>
static int loot_matches(int quality, int known, uint64_t unit_value, int value_known, int container,
                        unsigned mask, uint64_t minimum, int show_containers,
                        float distance, float maximum_distance) {
    if (!isfinite(distance) || distance < 0 || !isfinite(maximum_distance) ||
        distance > maximum_distance || (container && !show_containers)) return 0;
    int q = known && quality >= 0 && quality <= 7 ? quality : 0;
    return (mask & (1u << q)) && (!minimum || (value_known && unit_value >= minimum));
}
#endif
