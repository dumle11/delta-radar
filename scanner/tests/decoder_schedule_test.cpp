#include "../decoder_schedule.h"
#include "../position_cache.h"
#include <cassert>
#include <cstdio>

int main() {
    using Source = PositionSource;
    DecodeSchedule schedule;
    assert(schedule.set_context(42, 0x7000000000));
    assert(!schedule.set_context(42, 0x7000000000));

    // A burst of character work cannot sit behind loot or starve it forever.
    assert(schedule.request(0x1000, Source::Transform, 0));
    for (uint64_t root = 0x2000; root < 0x2040; root += 0x10)
        assert(schedule.request(root, Source::CharacterBounds, 0));
    for (int i = 0; i < 5; ++i) {
        auto next = schedule.take(1); assert(next);
        assert(next->source == (i == 3 ? Source::Transform : Source::CharacterBounds));
        assert(schedule.finish(*next, true, 2));
    }
    assert(!schedule.take(3));

    // Repeated frames coalesce. Different sources on one root stay independent.
    assert(schedule.request(0x3000, Source::CharacterBounds, 10));
    auto active = schedule.take(10); assert(active);
    for (int i = 11; i < 200; ++i)
        assert(!schedule.request(0x3000, Source::CharacterBounds, i));
    assert(schedule.request(0x3000, Source::Transform, 199));
    assert(schedule.finish(*active, false, 200));
    assert(!schedule.request(0x3000, Source::CharacterBounds, 449));
    assert(schedule.request(0x3000, Source::CharacterBounds, 450));
    auto bounds = schedule.take(450); assert(bounds && bounds->source == Source::CharacterBounds);
    assert(schedule.finish(*bounds, true, 451));
    assert(!schedule.take(451)); // Untouched loot expired while bounds were retried.

    // Pending work stays useful only while a live actor keeps requesting it.
    assert(schedule.request(0x4000, Source::CharacterBounds, 500));
    assert(!schedule.request(0x4000, Source::CharacterBounds, 700));
    auto touched = schedule.take(800); assert(touched);
    assert(schedule.finish(*touched, true, 801));
    assert(schedule.request(0x4000, Source::CharacterBounds, 900));
    assert(!schedule.take(1150));

    // A late old-process completion cannot erase or satisfy a new request.
    assert(schedule.request(0x5000, Source::CharacterBounds, 1200));
    auto old = schedule.take(1200); assert(old);
    assert(schedule.set_context(43, 0x7100000000));
    assert(schedule.request(0x5000, Source::CharacterBounds, 1201));
    assert(!schedule.finish(*old, true, 1202));
    auto fresh = schedule.take(1202); assert(fresh && fresh->pid == 43);
    assert(schedule.finish(*fresh, true, 1203));

    // Capacity includes the active decode; bursts cannot grow memory unboundedly.
    for (std::size_t i = 0; i < DecodeSchedule::Capacity; ++i)
        assert(schedule.request(0x6000 + i * 8, Source::CharacterBounds, 1300));
    assert(!schedule.request(0x9000, Source::CharacterBounds, 1300));
    auto busy = schedule.take(1301); assert(busy);
    assert(!schedule.request(0x9000, Source::CharacterBounds, 1301));
    assert(schedule.finish(*busy, true, 1302));
    assert(schedule.request(0x9000, Source::CharacterBounds, 1302));
    assert(schedule.set_context(43, 0x7200000000));
    assert(!schedule.has_queued());
    std::unordered_map<uint64_t, long long> values;
    for (uint64_t i = 1; i <= 512; i++) values[i] = 1000;
    values[7] = 900;
    auto timestamp = [](long long at){return at;};
    // Refreshing an existing entry never evicts unrelated live coordinates.
    make_position_cache_room(values, 1, 1100, timestamp);
    assert(values.size() == 512);
    make_position_cache_room(values, 513, 1100, timestamp);
    assert(values.size() == 511 && !values.count(7) && values.count(1));
    values[513] = 1100;
    assert(values.size() == 512);
    // Expired entries, rather than the whole live cache, are removed first.
    values[2] = 0; values[3] = 0;
    make_position_cache_room(values, 514, 1100, timestamp);
    assert(values.size() == 510 && !values.count(2) && !values.count(3) && values.count(513));
    puts("Decoder priority, coalescing, backoff, expiry, context, capacity and cache eviction checks passed");
}
