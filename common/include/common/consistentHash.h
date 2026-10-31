#pragma once

/// @file consistentHash.h
/// @brief Consistent hashing ring with virtual nodes.
///
/// Maps keys to nodes in a way that minimizes remapping when nodes are added
/// or removed. Uses virtual nodes to improve distribution uniformity — each
/// physical node is placed at multiple points on the ring.
///
/// Hash function: FNV-1a (fast, good distribution, no external dependency).
///
/// When a node is added, approximately 1/N of the key space remaps to it.
/// When a node is removed, only its keys remap to the next node on the ring.

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace outpost {
namespace common {

/// @brief Consistent hashing ring with configurable virtual nodes per physical node.
///
/// Thread-safe: all methods are protected by a mutex.
class ConsistentHashRing {
public:
    /// @param virtualNodesPerNode Number of virtual nodes per physical node.
    ///        Higher values give better distribution but use more memory.
    ///        Recommended: 100–200 for production use.
    explicit ConsistentHashRing(std::size_t virtualNodesPerNode = 150);

    /// @brief Add a physical node to the ring.
    /// @param nodeId Unique identifier for the node (e.g., "edge-1").
    void addNode(const std::string& nodeId);

    /// @brief Remove a physical node from the ring.
    /// @param nodeId The node to remove.
    void removeNode(const std::string& nodeId);

    /// @brief Look up which node a key maps to.
    /// @param key The key to route.
    /// @return The nodeId that owns this key, or empty string if ring is empty.
    std::string getNode(const std::string& key) const;

    /// @brief Return the number of physical nodes on the ring.
    std::size_t nodeCount() const;

    /// @brief Return the total number of virtual nodes on the ring.
    std::size_t virtualNodeCount() const;

    /// @brief Return a list of all physical node IDs.
    std::vector<std::string> getNodes() const;

    /// @brief Check if a specific node is on the ring.
    bool hasNode(const std::string& nodeId) const;

    /// @brief FNV-1a hash function (publicly exposed for testing).
    static uint64_t hash(const std::string& key);

private:
    std::size_t virtualNodesPerNode_;
    mutable std::mutex mutex_;

    // ring_: hash value → physical node ID.
    // Using std::map (ordered) so we can do upper_bound lookups efficiently.
    std::map<uint64_t, std::string> ring_;

    // Track which physical nodes are present.
    std::vector<std::string> nodes_;
};

}  // namespace common
}  // namespace outpost
