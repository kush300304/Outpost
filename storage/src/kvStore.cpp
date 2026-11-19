#include "storage/kvStore.h"

#include <shared_mutex>
#include <utility>

#include <spdlog/spdlog.h>

namespace outpost {
namespace storage {

KvStore::KvStore() = default;

KvStore::KvStore(const std::string& walFilePath)
    : wal_(std::make_unique<outpost::common::WriteAheadLog>(walFilePath)) {
    replayWal();
}

void KvStore::replayWal() {
    if (!wal_) return;

    std::size_t applied = 0;
    for (const common::WalEntry& entry : wal_->replay()) {
        if (entry.operation == common::WalOperation::PUT) {
            data_[entry.key] = entry.value;
        } else if (entry.operation == common::WalOperation::DELETE) {
            data_.erase(entry.key);
        }
        ++applied;
    }

    spdlog::info("KvStore: replayed {} WAL entries, {} keys in memory", applied, data_.size());
}

std::optional<std::string> KvStore::get(const std::string& key) const {
    std::shared_lock lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void KvStore::put(const std::string& key, const std::string& value) {
    std::unique_lock lock(mutex_);
    // WAL first (write-ahead), then in-memory update.
    if (wal_) {
        wal_->appendPut(key, value);
    }
    data_[key] = value;
}

bool KvStore::remove(const std::string& key) {
    std::unique_lock lock(mutex_);
    auto it = data_.find(key);
    if (it == data_.end()) {
        return false;
    }
    // WAL first, then in-memory delete.
    if (wal_) {
        wal_->appendDelete(key);
    }
    data_.erase(it);
    return true;
}

bool KvStore::exists(const std::string& key) const {
    std::shared_lock lock(mutex_);
    return data_.find(key) != data_.end();
}

std::size_t KvStore::size() const {
    std::shared_lock lock(mutex_);
    return data_.size();
}

std::vector<std::string> KvStore::getAllKeys() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> keys;
    keys.reserve(data_.size());
    for (const auto& [k, v] : data_) {
        keys.push_back(k);
    }
    return keys;
}

void KvStore::clear() {
    std::unique_lock lock(mutex_);
    data_.clear();
    if (wal_) {
        wal_->truncate();
    }
}

void KvStore::forEach(
    const std::function<void(const std::string& key, const std::string& value)>& fn) const {
    std::shared_lock lock(mutex_);
    for (const auto& [k, v] : data_) {
        fn(k, v);
    }
}

}  // namespace storage
}  // namespace outpost
