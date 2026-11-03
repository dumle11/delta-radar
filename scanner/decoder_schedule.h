#pragma once

#include <cstdint>
#include <cstddef>
#include <deque>
#include <optional>
#include <unordered_map>

enum class PositionSource : uint64_t { Transform = 0, CharacterBounds = 1 };

// Component pointers are eight-byte aligned; source occupies the low bit.
inline uint64_t decode_key(uint64_t root, PositionSource source) {
    return root | static_cast<uint64_t>(source);
}

// Accessed under the bridge's result mutex. Only the worker runs Unicorn.
class DecodeSchedule {
public:
    static constexpr long long RetryMs = 250;
    static constexpr long long UnusedRequestMs = 250;
    static constexpr std::size_t Capacity = 64;
    struct Request {
        int pid;
        uint64_t base, root;
        PositionSource source;
        uint64_t generation;
        long long lastSeen;
    };

    bool set_context(int pid, uint64_t base) {
        if (pid_ == pid && base_ == base) return false;
        pid_ = pid; base_ = base; ++generation_;
        pending_.clear(); retryAfter_.clear();
        characters_.clear(); transforms_.clear(); characterBurst_ = 0;
        return true;
    }

    bool request(uint64_t root, PositionSource source, long long now) {
        const auto key = decode_key(root, source);
        auto pending = pending_.find(key);
        if (pending != pending_.end()) {
            pending->second.lastSeen = now;
            return false;
        }
        auto retry = retryAfter_.find(key);
        if (retry != retryAfter_.end() && now < retry->second) return false;
        if (pending_.size() >= Capacity) return false;
        retryAfter_.erase(key);
        pending_.emplace(key, Request{pid_, base_, root, source, generation_, now});
        (source == PositionSource::CharacterBounds ? characters_ : transforms_).push_back(key);
        return true;
    }

    bool has_queued() const { return !characters_.empty() || !transforms_.empty(); }

    std::optional<Request> take(long long now) {
        while (has_queued()) {
            // Characters get priority, but loot always receives a turn.
            bool character = !characters_.empty() && (transforms_.empty() || characterBurst_ < 3);
            auto &queue = character ? characters_ : transforms_;
            const auto key = queue.front(); queue.pop_front();
            auto entry = pending_.find(key);
            if (entry == pending_.end()) continue;
            if (now - entry->second.lastSeen >= UnusedRequestMs) {
                pending_.erase(entry);
                continue;
            }
            characterBurst_ = character ? (characterBurst_ < 3 ? characterBurst_ + 1 : 3) : 0;
            return entry->second;
        }
        return std::nullopt;
    }

    // A completion from the old game process must not replace a new result.
    bool finish(const Request &request, bool success, long long now) {
        if (request.generation != generation_) return false;
        const auto key = decode_key(request.root, request.source);
        pending_.erase(key);
        if (success) retryAfter_.erase(key);
        else {
            if (retryAfter_.size() >= 512) {
                for (auto it = retryAfter_.begin(); it != retryAfter_.end();) {
                    if (it->second <= now) it = retryAfter_.erase(it); else ++it;
                }
                if (retryAfter_.size() >= 512) retryAfter_.erase(retryAfter_.begin());
            }
            retryAfter_[key] = now + RetryMs;
        }
        return true;
    }

private:
    int pid_ = 0;
    uint64_t base_ = 0, generation_ = 0;
    unsigned characterBurst_ = 0;
    std::deque<uint64_t> characters_, transforms_;
    std::unordered_map<uint64_t, Request> pending_;
    std::unordered_map<uint64_t, long long> retryAfter_;
};
