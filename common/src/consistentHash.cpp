#include "common/consistentHash.h"

#include <algorithm>
#include <spdlog/spdlog.h>

namespace outpost {
namespace common {

// ── FNV-1a 64-bit hash ──────────────────────────────────────────────────
// Fast, well-distributed, zero external dependencies. Same algorithm used
// by many hash table implementations.

static constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
static constexpr uint64_t kFnvPrime = 1099511628211ULL;

uint64_t ConsistentHashRing::hash(const std::string& key) {
    uint64_t h = kFnvOffsetBasis;
    for (unsigned char c : key) {
        h ^= static_cast<uint64_t>(c);
        h *= kFnvPrime;
    }
    return h;
}

ConsistentHashRing::ConsistentHashRing(std::size_t virtualNodesPerNode)
    : virtualNodesPerNode_(virtualNodesPerNode) {
}

void ConsistentHashRing::addNode(const std::string& nodeId) {
    std::lock_guard lock(mutex_);

    // Check if already present
    if (std::find(nodes_.begin(), nodes_.end(), nodeId) != nodes_.end()) {
        spdlog::warn("ConsistentHashRing: node '{}' already on ring, skipping", nodeId);
        return;
    }

    nodes_.push_back(nodeId);

    // Place virtual nodes on the ring
    for (std::size_t i = 0; i < virtualNodesPerNode_; ++i) {
        // Virtual node key: "nodeId#i" — hashed to get ring position
        std::string virtualKey = nodeId + "#" + std::to_string(i);
        uint64_t h = hash(virtualKey);
        ring_[h] = nodeId;
    }

    spdlog::info("ConsistentHashRing: added node '{}' ({} virtual nodes, {} total on ring)",
                 nodeId, virtualNodesPerNode_, ring_.size());
}

void ConsistentHashRing::removeNode(const std::string& nodeId) {
    std::lock_guard lock(mutex_);

    auto it = std::find(nodes_.begin(), nodes_.end(), nodeId);
    if (it == nodes_.end()) {
        spdlog::warn("ConsistentHashRing: node '{}' not on ring, skipping removal", nodeId);
        return;
    }

    nodes_.erase(it);

    // Remove all virtual nodes for this physical node
    for (std::size_t i = 0; i < virtualNodesPerNode_; ++i) {
        std::string virtualKey = nodeId + "#" + std::to_string(i);
        uint64_t h = hash(virtualKey);
        ring_.erase(h);
    }

    spdlog::info("ConsistentHashRing: removed node '{}' ({} remaining on ring)", nodeId,
                 ring_.size());
}

std::string ConsistentHashRing::getNode(const std::string& key) const {
    std::lock_guard lock(mutex_);

    if (ring_.empty()) {
        return "";
    }

    uint64_t h = hash(key);

    // Find the first virtual node with hash >= h (clockwise on the ring).
    auto it = ring_.lower_bound(h);
    if (it == ring_.end()) {
        // Wrap around to the beginning of the ring
        it = ring_.begin();
    }

    return it->second;
}

std::size_t ConsistentHashRing::nodeCount() const {
    std::lock_guard lock(mutex_);
    return nodes_.size();
}

std::size_t ConsistentHashRing::virtualNodeCount() const {
    std::lock_guard lock(mutex_);
    return ring_.size();
}

std::vector<std::string> ConsistentHashRing::getNodes() const {
    std::lock_guard lock(mutex_);
    return nodes_;
}

bool ConsistentHashRing::hasNode(const std::string& nodeId) const {
    std::lock_guard lock(mutex_);
    return std::find(nodes_.begin(), nodes_.end(), nodeId) != nodes_.end();
}

}  // namespace common
}  // namespace outpost
