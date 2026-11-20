#pragma once

/// @file lruCache.h
/// @brief Thread-safe LRU cache with per-entry freshness and write versions.
///
/// Fixed capacity with O(1) lookup, insert and eviction: a doubly-linked list keeps recency
/// (front = most recently used) and an unordered_map indexes the list nodes.
///
/// Freshness: an entry is fresh for `ttl` after it was last put (a zero ttl never expires).
/// Expired entries stay cached until evicted, so a caller can still serve them as stale data when
/// the source of truth is unavailable; get() returns only fresh entries, lookup() returns any.
///
/// Versions: put() carries the version of the data it stores and is ignored when the cached
/// entry already holds a newer version, so a slow fetch can never replace a newer write.
///
/// Thread safety: every public method takes the cache's mutex. forEach() runs its callback under
/// that mutex, so the callback must not call back into the cache.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace outpost
{
namespace common
{

/// @brief Fixed-capacity LRU cache with freshness and version-aware puts.
class LruCache
{
public:
    using Key = std::string;
    using Value = std::string;

    /// @brief A cached value and whether it is still within its ttl.
    struct Lookup
    {
        Value value;
        bool fresh = true;
    };

    /// @brief Hit/miss/eviction counters.
    struct Stats
    {
        uint64_t hits = 0;       ///< Lookups that found a fresh entry.
        uint64_t misses = 0;     ///< Lookups that found nothing or an expired entry.
        uint64_t evictions = 0;  ///< Entries evicted to make room.
        uint64_t puts = 0;       ///< put() calls, including ignored ones.

        /// @brief Hit rate as a percentage (0.0–100.0); 0.0 before the first lookup.
        double hitRate() const
        {
            uint64_t total = hits + misses;
            return total > 0 ? (static_cast<double>(hits) / static_cast<double>(total)) * 100.0
                             : 0.0;
        }
    };

    /// @param capacity Maximum number of entries; must be > 0.
    /// @param ttl      How long an entry stays fresh after a put; zero means it never expires.
    /// @throws std::invalid_argument when capacity is 0.
    explicit LruCache(std::size_t capacity,
                      std::chrono::milliseconds ttl = std::chrono::milliseconds(0));

    LruCache(const LruCache&) = delete;
    LruCache& operator=(const LruCache&) = delete;
    LruCache(LruCache&&) = delete;
    LruCache& operator=(LruCache&&) = delete;

    /// @brief Fresh value for key, promoted to most recently used; nullopt when absent or expired.
    std::optional<Value> get(const Key& key);

    /// @brief Any cached value for key with its freshness, promoted to most recently used.
    ///
    /// Counts a hit when the entry is fresh and a miss otherwise.
    std::optional<Lookup> lookup(const Key& key);

    /// @brief Insert or update key, restarting its ttl and making it most recently used.
    /// @param version Version of value; the put is ignored when the cached entry is newer.
    /// @return false when the put was ignored because a newer version is cached.
    bool put(const Key& key, const Value& value, uint64_t version = 0);

    /// @brief Whether key is cached (fresh or expired), without promoting it.
    bool contains(const Key& key) const;

    /// @brief Remove key. @return true when it was cached.
    bool remove(const Key& key);

    /// @brief Number of cached entries (fresh and expired).
    std::size_t size() const;

    /// @brief Maximum number of entries.
    std::size_t capacity() const
    {
        return capacity_;
    }

    /// @brief Freshness lifetime of an entry; zero means entries never expire.
    std::chrono::milliseconds ttl() const
    {
        return ttl_;
    }

    /// @brief Snapshot of the counters.
    Stats stats() const;

    /// @brief Remove every entry and reset the counters.
    void clear();

    /// @brief Call fn for every entry, most recently used first, under the cache's mutex.
    void forEach(const std::function<void(const Key& key, const Value& value)>& fn) const;

private:
    using Clock = std::chrono::steady_clock;

    struct Entry
    {
        Key key;
        Value value;
        uint64_t version = 0;
        Clock::time_point storedAt;
    };
    using ListType = std::list<Entry>;

    const std::size_t capacity_;
    const std::chrono::milliseconds ttl_;
    mutable std::mutex mutex_;
    ListType order_;  ///< Front = most recently used.
    std::unordered_map<Key, ListType::iterator> index_;
    Stats stats_;
};

}  // namespace common
}  // namespace outpost
