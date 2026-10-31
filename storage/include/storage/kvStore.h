#pragma once

/// @file kvStore.h
/// @brief Thread-safe in-memory key-value store with optional WAL durability.
///
/// When constructed with a WAL file path, every mutation is written to the WAL
/// before updating the in-memory map. On construction, the WAL is replayed to
/// rebuild state from disk, providing crash-recovery semantics.
///
/// Without a WAL path, operates as a pure in-memory store (useful for testing).

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/wal.h"

namespace outpost {
namespace storage {

/// @brief Thread-safe in-memory key-value store with optional WAL persistence.
///
/// Concurrency model:
///   - Reads (get, exists, size, getAllKeys) acquire a shared lock.
///   - Writes (put, remove, clear) acquire an exclusive lock.
///
/// All public methods are safe to call from multiple threads concurrently.
class KvStore {
public:
    /// @brief Construct a pure in-memory store (no durability).
    KvStore();

    /// @brief Construct a durable store backed by a WAL file.
    /// @param walFilePath Path to the WAL file. Created if it doesn't exist.
    ///                    On construction, the WAL is replayed to rebuild state.
    explicit KvStore(const std::string& walFilePath);

    ~KvStore() = default;

    // Non-copyable, non-movable (owns a mutex and possibly a WAL).
    KvStore(const KvStore&) = delete;
    KvStore& operator=(const KvStore&) = delete;
    KvStore(KvStore&&) = delete;
    KvStore& operator=(KvStore&&) = delete;

    /// @brief Retrieve the value associated with a key.
    /// @param key The key to look up.
    /// @return The value if the key exists, or std::nullopt if it does not.
    std::optional<std::string> get(const std::string& key) const;

    /// @brief Insert or update a key-value pair.
    /// @param key   The key to set.
    /// @param value The value to associate with the key.
    ///
    /// If a WAL is configured, the write is logged before the in-memory update.
    void put(const std::string& key, const std::string& value);

    /// @brief Remove a key-value pair.
    /// @param key The key to remove.
    /// @return true if the key was found and removed, false if the key did not exist.
    bool remove(const std::string& key);

    /// @brief Check whether a key exists in the store.
    bool exists(const std::string& key) const;

    /// @brief Return the number of key-value pairs currently stored.
    std::size_t size() const;

    /// @brief Return a snapshot of all keys currently in the store.
    std::vector<std::string> getAllKeys() const;

    /// @brief Remove all entries from the store. Also truncates the WAL if present.
    void clear();

    /// @brief Execute a callback for each key-value pair while holding a read lock.
    void forEach(const std::function<void(const std::string& key,
                                          const std::string& value)>& fn) const;

    /// @brief Return whether this store is backed by a WAL.
    bool isDurable() const { return wal_ != nullptr; }

private:
    /// @brief Replay the WAL to rebuild in-memory state. Called during construction.
    void replayWal();

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::string> data_;
    std::unique_ptr<outpost::common::WriteAheadLog> wal_;
};

}  // namespace storage
}  // namespace outpost
