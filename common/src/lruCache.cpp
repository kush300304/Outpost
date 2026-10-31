#include "common/lruCache.h"

#include <stdexcept>

namespace outpost {
namespace common {

LruCache::LruCache(std::size_t capacity) : capacity_(capacity) {
    if (capacity == 0) {
        throw std::invalid_argument("LruCache capacity must be > 0");
    }
}

std::optional<LruCache::Value> LruCache::get(const Key& key) {
    std::lock_guard lock(mutex_);
    auto it = index_.find(key);
    if (it == index_.end()) {
        ++stats_.misses;
        return std::nullopt;
    }

    // Move accessed entry to front (most recently used).
    order_.splice(order_.begin(), order_, it->second);
    ++stats_.hits;
    return it->second->second;
}

void LruCache::put(const Key& key, const Value& value) {
    std::lock_guard lock(mutex_);
    ++stats_.puts;

    auto it = index_.find(key);
    if (it != index_.end()) {
        // Key exists — update value and move to front.
        it->second->second = value;
        order_.splice(order_.begin(), order_, it->second);
        return;
    }

    // Key doesn't exist — evict LRU if at capacity.
    if (order_.size() >= capacity_) {
        auto& lruEntry = order_.back();
        index_.erase(lruEntry.first);
        order_.pop_back();
        ++stats_.evictions;
    }

    // Insert new entry at front.
    order_.emplace_front(key, value);
    index_[key] = order_.begin();
}

bool LruCache::contains(const Key& key) const {
    std::lock_guard lock(mutex_);
    return index_.find(key) != index_.end();
}

bool LruCache::remove(const Key& key) {
    std::lock_guard lock(mutex_);
    auto it = index_.find(key);
    if (it == index_.end()) {
        return false;
    }
    order_.erase(it->second);
    index_.erase(it);
    return true;
}

std::size_t LruCache::size() const {
    std::lock_guard lock(mutex_);
    return order_.size();
}

LruCache::Stats LruCache::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

void LruCache::clear() {
    std::lock_guard lock(mutex_);
    order_.clear();
    index_.clear();
    stats_ = Stats{};
}

void LruCache::forEach(
    const std::function<void(const Key& key, const Value& value)>& fn) const {
    std::lock_guard lock(mutex_);
    for (const auto& [k, v] : order_) {
        fn(k, v);
    }
}

}  // namespace common
}  // namespace outpost
