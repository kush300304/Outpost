#pragma once

/// @file lruCache.h
/// @brief Thread-safe LRU (Least Recently Used) cache.
///
/// Implements a fixed-capacity key-value cache with O(1) get, put, and eviction.
/// Backed by a doubly-linked list (for LRU ordering) and an unordered_map
/// (for O(1) key lookup). When the cache is full, the least-recently-used
/// entry is evicted to make room for new entries.
///
/// This is the core caching primitive used by edge nodes. Each edge maintains
/// its own LruCache to avoid redundant origin fetches.
///
/// Thread safety: all public methods are protected by a mutex. Concurrent
/// access from multiple request-handling threads is safe.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace outpost {
namespace common {

/// @brief Fixed-capacity LRU cache with O(1) operations.
///
/// Implementation:
///   - A std::list<pair<K,V>> maintains insertion/access order (front = most recent).
///   - A std::unordered_map<K, list::iterator> provides O(1) key→node lookup.
///   - On get: move the accessed node to the front of the list.
///   - On put: if key exists, update and move to front. If full, evict from back.
class LruCache {
public:
    using Key = std::string;
    using Value = std::string;

    /// @brief Construct an LRU cache with the given maximum capacity.
    /// @param capacity Maximum number of entries. Must be > 0.
    explicit LruCache(std::size_t capacity);

    ~LruCache() = default;

    // Non-copyable, non-movable (owns a mutex).
    LruCache(const LruCache&) = delete;
    LruCache& operator=(const LruCache&) = delete;
    LruCache(LruCache&&) = delete;
    LruCache& operator=(LruCache&&) = delete;

    /// @brief Look up a key in the cache.
    ///
    /// If found, the entry is promoted to most-recently-used.
    /// @param key The key to look up.
    /// @return The cached value, or std::nullopt on a miss.
    std::optional<Value> get(const Key& key);

    /// @brief Insert or update a key-value pair.
    ///
    /// If the key already exists, its value is updated and it becomes the MRU.
    /// If the cache is at capacity, the LRU entry is evicted first.
    /// @param key   The key.
    /// @param value The value to cache.
    void put(const Key& key, const Value& value);

    /// @brief Check if a key exists in the cache without promoting it.
    bool contains(const Key& key) const;

    /// @brief Remove a specific key from the cache.
    /// @return true if the key was found and removed.
    bool remove(const Key& key);

    /// @brief Return the number of entries currently in the cache.
    std::size_t size() const;

    /// @brief Return the maximum capacity of the cache.
    std::size_t capacity() const { return capacity_; }

    /// @brief Return cache statistics.
    struct Stats {
        uint64_t hits = 0;       ///< Number of successful get() calls.
        uint64_t misses = 0;     ///< Number of get() calls that returned nullopt.
        uint64_t evictions = 0;  ///< Number of entries evicted due to capacity.
        uint64_t puts = 0;       ///< Total put() calls.

        /// @brief Hit rate as a percentage (0.0–100.0). Returns 0.0 if no gets.
        double hitRate() const {
            uint64_t total = hits + misses;
            return total > 0 ? (static_cast<double>(hits) / static_cast<double>(total)) * 100.0
                             : 0.0;
        }
    };

    /// @brief Return a snapshot of cache statistics.
    Stats stats() const;

    /// @brief Clear all entries from the cache. Resets statistics.
    void clear();

    /// @brief Iterate over all entries (most-recent first). Holds lock for duration.
    void forEach(const std::function<void(const Key& key, const Value& value)>& fn) const;

private:
    using ListEntry = std::pair<Key, Value>;
    using ListType = std::list<ListEntry>;
    using MapType = std::unordered_map<Key, typename ListType::iterator>;

    std::size_t capacity_;
    mutable std::mutex mutex_;
    ListType order_;  ///< Front = MRU, back = LRU.
    MapType index_;
    Stats stats_;
};

}  // namespace common
}  // namespace outpost
