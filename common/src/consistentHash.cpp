/// @file consistentHash.cpp
/// @brief Consistent hashing ring: FNV-1a + fmix64 hash, (hash, nodeId) ring points.

#include "common/consistentHash.h"

#include <stdexcept>

namespace outpost {
namespace common {

namespace {

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
constexpr uint64_t kFnvPrime = 1099511628211ULL;

}  // namespace

uint64_t ConsistentHashRing::hash(const std::string& key) {
    uint64_t h = kFnvOffsetBasis;
    for (unsigned char c : key) {
        h ^= static_cast<uint64_t>(c);
        h *= kFnvPrime;
    }
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

ConsistentHashRing::ConsistentHashRing(std::size_t virtualNodesPerNode)
    : virtualNodesPerNode_(virtualNodesPerNode) {
    if (virtualNodesPerNode_ == 0) {
        throw std::invalid_argument("ConsistentHashRing: virtualNodesPerNode must be > 0");
    }
}

bool ConsistentHashRing::addNode(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool added = nodes_.insert(nodeId).second;
    for (std::size_t i = 0; added && i < virtualNodesPerNode_; ++i) {
        ring_.emplace(hash(nodeId + "#" + std::to_string(i)), nodeId);
    }
    return added;
}

bool ConsistentHashRing::removeNode(const std::string& nodeId) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool removed = nodes_.erase(nodeId) > 0;
    for (std::size_t i = 0; removed && i < virtualNodesPerNode_; ++i) {
        ring_.erase({hash(nodeId + "#" + std::to_string(i)), nodeId});
    }
    return removed;
}

std::string ConsistentHashRing::getNode(const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string owner;
    if (!ring_.empty()) {
        auto it = ring_.lower_bound({hash(key), std::string()});
        owner = it == ring_.end() ? ring_.begin()->second : it->second;
    }
    return owner;
}

std::size_t ConsistentHashRing::nodeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.size();
}

std::size_t ConsistentHashRing::virtualNodeCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ring_.size();
}

std::vector<std::string> ConsistentHashRing::getNodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<std::string>(nodes_.begin(), nodes_.end());
}

bool ConsistentHashRing::hasNode(const std::string& nodeId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nodes_.count(nodeId) > 0;
}

}  // namespace common
}  // namespace outpost
