#pragma once

/// @file kvStore.h
/// @brief Thread-safe versioned key-value store with optional WAL durability.
///
/// Every value carries the (version, writerId) pair of the write that produced it. A put is
/// applied only when its pair is newer than the stored pair (last-writer-wins): the same write
/// arriving twice is a DUPLICATE and an older write is SUPERSEDED, so replaying queued writes is
/// idempotent and never overwrites newer data. Only applied writes are appended to the WAL, and
/// the WAL is replayed with the same rule on construction.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "common/wal.h"

namespace outpost
{
namespace storage
{

/// @brief Result of a versioned put.
enum class PutOutcome
{
    APPLIED,     ///< The write was newer and is now the stored value.
    DUPLICATE,   ///< The same (version, writerId) is already stored; nothing changed.
    SUPERSEDED,  ///< A newer write is already stored; nothing changed.
};

/// @brief Human-readable name of a PutOutcome ("applied", "duplicate", "superseded").
const char* putOutcomeToString(PutOutcome outcome);

/// @brief A stored value with the identity of the write that produced it.
struct VersionedValue
{
    std::string value;
    uint64_t version = 0;
    std::string writerId;
};

/// @brief Thread-safe versioned key-value store.
///
/// Reads take a shared lock; puts take an exclusive lock that also covers the WAL append, so the
/// log order always matches the order in which writes were applied.
class KvStore
{
public:
    /// @brief Counters of put outcomes since construction.
    struct Counters
    {
        uint64_t applied = 0;
        uint64_t duplicates = 0;
        uint64_t superseded = 0;
    };

    /// @brief Construct a pure in-memory store.
    KvStore();

    /// @brief Construct a durable store; replays the WAL at walFilePath to rebuild state.
    /// @param syncOnAppend fsync each applied write before put() returns.
    explicit KvStore(const std::string& walFilePath, bool syncOnAppend = true);

    KvStore(const KvStore&) = delete;
    KvStore& operator=(const KvStore&) = delete;
    KvStore(KvStore&&) = delete;
    KvStore& operator=(KvStore&&) = delete;

    /// @brief Return the stored value for key, if any.
    std::optional<VersionedValue> get(const std::string& key) const;

    /// @brief Store value under key when (version, writerId) is newer than the stored pair.
    PutOutcome put(const std::string& key, const std::string& value, uint64_t version,
                   const std::string& writerId);

    /// @brief Whether key has a stored value.
    bool exists(const std::string& key) const;

    /// @brief Number of stored keys.
    std::size_t size() const;

    /// @brief Snapshot of all stored keys.
    std::vector<std::string> getAllKeys() const;

    /// @brief Call fn for every key and value while holding a shared lock.
    void forEach(
        const std::function<void(const std::string& key, const std::string& value)>& fn) const;

    /// @brief Whether this store is backed by a WAL.
    bool isDurable() const;

    /// @brief Put outcome counters.
    Counters counters() const;

    /// @brief What WAL recovery found at construction (all zero for an in-memory store).
    common::WalRecoveryReport recoveryReport() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, VersionedValue> data_;
    std::unique_ptr<common::WriteAheadLog> wal_;
    Counters counters_;
};

}  // namespace storage
}  // namespace outpost
