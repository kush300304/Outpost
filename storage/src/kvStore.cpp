/// @file kvStore.cpp
/// @brief Versioned key-value store: last-writer-wins puts, WAL logging and replay.

#include "storage/kvStore.h"

#include <mutex>
#include <tuple>
#include <utility>

namespace outpost
{
namespace storage
{

namespace
{

/// Compares a write's (version, writerId) with the stored one.
PutOutcome compareWrite(const VersionedValue* stored, uint64_t version, const std::string& writerId)
{
    PutOutcome outcome = PutOutcome::APPLIED;
    if (stored != nullptr)
    {
        auto incoming = std::tie(version, writerId);
        auto existing = std::tie(stored->version, stored->writerId);
        if (incoming == existing)
        {
            outcome = PutOutcome::DUPLICATE;
        }
        else if (incoming < existing)
        {
            outcome = PutOutcome::SUPERSEDED;
        }
    }
    return outcome;
}

}  // namespace

const char* putOutcomeToString(PutOutcome outcome)
{
    const char* name = "applied";
    if (outcome == PutOutcome::DUPLICATE)
    {
        name = "duplicate";
    }
    else if (outcome == PutOutcome::SUPERSEDED)
    {
        name = "superseded";
    }
    return name;
}

KvStore::KvStore() = default;

KvStore::KvStore(const std::string& walFilePath, bool syncOnAppend)
    : wal_(std::make_unique<common::WriteAheadLog>(walFilePath, syncOnAppend))
{
    for (common::WalEntry& entry : wal_->takeRecoveredEntries())
    {
        auto it = data_.find(entry.key);
        const VersionedValue* stored = it == data_.end() ? nullptr : &it->second;
        if (entry.operation == common::WalOperation::PUT &&
            compareWrite(stored, entry.version, entry.writerId) == PutOutcome::APPLIED)
        {
            data_[entry.key] =
                VersionedValue{std::move(entry.value), entry.version, std::move(entry.writerId)};
        }
    }
}

std::optional<VersionedValue> KvStore::get(const std::string& key) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::optional<VersionedValue> result;
    auto it = data_.find(key);
    if (it != data_.end())
    {
        result = it->second;
    }
    return result;
}

PutOutcome KvStore::put(const std::string& key, const std::string& value, uint64_t version,
                        const std::string& writerId)
{
    PutOutcome outcome = PutOutcome::APPLIED;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = data_.find(key);
        outcome = compareWrite(it == data_.end() ? nullptr : &it->second, version, writerId);
    }
    // Make the write durable without holding the map lock, so concurrent puts share one fsync.
    if (outcome == PutOutcome::APPLIED && wal_)
    {
        common::WalEntry entry;
        entry.operation = common::WalOperation::PUT;
        entry.key = key;
        entry.value = value;
        entry.version = version;
        entry.writerId = writerId;
        wal_->append(entry);
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (outcome == PutOutcome::APPLIED)
    {
        // A newer write may have become durable while this one was being logged; the logged record
        // is then superseded on replay too, so the map and the log always agree.
        auto it = data_.find(key);
        outcome = compareWrite(it == data_.end() ? nullptr : &it->second, version, writerId);
        if (outcome == PutOutcome::APPLIED)
        {
            data_[key] = VersionedValue{value, version, writerId};
        }
    }
    if (outcome == PutOutcome::APPLIED)
    {
        ++counters_.applied;
    }
    else if (outcome == PutOutcome::DUPLICATE)
    {
        ++counters_.duplicates;
    }
    else
    {
        ++counters_.superseded;
    }
    return outcome;
}

bool KvStore::exists(const std::string& key) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return data_.find(key) != data_.end();
}

std::size_t KvStore::size() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return data_.size();
}

std::vector<std::string> KvStore::getAllKeys() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> keys;
    keys.reserve(data_.size());
    for (const auto& [key, stored] : data_)
    {
        keys.push_back(key);
    }
    return keys;
}

void KvStore::forEach(
    const std::function<void(const std::string& key, const std::string& value)>& fn) const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& [key, stored] : data_)
    {
        fn(key, stored.value);
    }
}

bool KvStore::isDurable() const
{
    return wal_ != nullptr;
}

KvStore::Counters KvStore::counters() const
{
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return counters_;
}

common::WalStats KvStore::walStats() const
{
    return wal_ ? wal_->stats() : common::WalStats{};
}

common::WalRecoveryReport KvStore::recoveryReport() const
{
    return wal_ ? wal_->recoveryReport() : common::WalRecoveryReport{};
}

}  // namespace storage
}  // namespace outpost
