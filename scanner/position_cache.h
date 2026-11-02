#pragma once

#include <cstdint>

// Make room without flushing every live marker when old actors churn the cache.
template <typename Map, typename Timestamp>
void make_position_cache_room(Map &values, uint64_t key, long long now, Timestamp timestamp) {
    if (values.size() < 512 || values.count(key)) return;
    for (auto it = values.begin(); it != values.end();) {
        if (now - timestamp(it->second) >= 750) it = values.erase(it);
        else ++it;
    }
    if (values.size() < 512) return;
    auto oldest = values.begin();
    for (auto it = values.begin(); it != values.end(); ++it)
        if (timestamp(it->second) < timestamp(oldest->second)) oldest = it;
    values.erase(oldest);
}
