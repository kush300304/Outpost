/// @file lruCache.cpp
/// @brief LRU cache implementation: recency list, index map, freshness and versions.

#include "common/lruCache.h"

#include <stdexcept>

namespace outpost
{
namespace common
{

LruCache::LruCache(std::size_t capacity, std::chrono::milliseconds ttl)
    : capacity_(capacity), ttl_(ttl)
{
    if (capacity_ == 0)
    {
        throw std::invalid_argument("LruCache capacity must be > 0");
    }
}

std::optional<LruCache::Lookup> LruCache::lookup(const Key& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::optional<Lookup> result;
    auto it = index_.find(key);
    if (it != index_.end())
    {
        order_.splice(order_.begin(), order_, it->second);
        bool fresh = ttl_.count() == 0 || Clock::now() - it->second->storedAt < ttl_;
        result = Lookup{it->second->value, fresh};
    }
    if (result && result->fresh)
    {
        ++stats_.hits;
    }
    else
    {
        ++stats_.misses;
    }
    return result;
}

std::optional<LruCache::Value> LruCache::get(const Key& key)
{
    std::optional<Value> value;
    std::optional<Lookup> entry = lookup(key);
    if (entry && entry->fresh)
    {
        value = std::move(entry->value);
    }
    return value;
}

bool LruCache::put(const Key& key, const Value& value, uint64_t version)
{
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.puts;
    auto it = index_.find(key);
    bool stored = it == index_.end() || version >= it->second->version;
    if (stored && it != index_.end())
    {
        it->second->value = value;
        it->second->version = version;
        it->second->storedAt = Clock::now();
        order_.splice(order_.begin(), order_, it->second);
    }
    else if (stored)
    {
        if (order_.size() >= capacity_)
        {
            index_.erase(order_.back().key);
            order_.pop_back();
            ++stats_.evictions;
        }
        order_.push_front(Entry{key, value, version, Clock::now()});
        index_[key] = order_.begin();
    }
    return stored;
}

bool LruCache::contains(const Key& key) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return index_.find(key) != index_.end();
}

bool LruCache::remove(const Key& key)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = index_.find(key);
    bool removed = it != index_.end();
    if (removed)
    {
        order_.erase(it->second);
        index_.erase(it);
    }
    return removed;
}

std::size_t LruCache::size() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return order_.size();
}

LruCache::Stats LruCache::stats() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

void LruCache::clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    order_.clear();
    index_.clear();
    stats_ = Stats{};
}

void LruCache::forEach(const std::function<void(const Key& key, const Value& value)>& fn) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (const Entry& entry : order_)
    {
        fn(entry.key, entry.value);
    }
}

}  // namespace common
}  // namespace outpost
