#pragma once

/// @file consistentHash.h
/// @brief Consistent hashing ring with virtual nodes.
///
/// Each physical node is placed at virtualNodesPerNode points on a 64-bit ring; a key belongs to
/// the first point clockwise from the key's hash. Adding a node moves only the keys that now fall
/// on the new node's points (about 1/(N+1) of them); removing a node moves only its own keys.
///
/// Hash: 64-bit FNV-1a followed by the MurmurHash3 fmix64 finalizer. FNV-1a alone leaves the
/// high bits of similar inputs ("edge-1#0", "edge-1#1", …) clustered, which skews ring ownership;
/// the finalizer spreads every input bit across the whole word.
///
/// Ring points are stored as (hash, nodeId) pairs, so two nodes whose points collide both stay on
/// the ring, ownership is the same whatever order nodes were added in, and removing a node never
/// removes another node's point.
///
/// Thread-safe: every method takes the ring's mutex.

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace outpost
{
namespace common
{

/// @brief Consistent hashing ring with configurable virtual nodes per physical node.
class ConsistentHashRing
{
public:
    /// @param virtualNodesPerNode Ring points per physical node (must be > 0).
    explicit ConsistentHashRing(std::size_t virtualNodesPerNode = 150);

    /// @brief Add a physical node. @return false when the node is already on the ring.
    bool addNode(const std::string& nodeId);

    /// @brief Remove a physical node. @return false when the node is not on the ring.
    bool removeNode(const std::string& nodeId);

    /// @brief The node that owns key, or an empty string when the ring is empty.
    std::string getNode(const std::string& key) const;

    /// @brief Number of physical nodes.
    std::size_t nodeCount() const;

    /// @brief Number of ring points.
    std::size_t virtualNodeCount() const;

    /// @brief Physical node ids, sorted.
    std::vector<std::string> getNodes() const;

    /// @brief Whether nodeId is on the ring.
    bool hasNode(const std::string& nodeId) const;

    /// @brief The ring hash: FNV-1a 64 followed by fmix64.
    static uint64_t hash(const std::string& key);

private:
    const std::size_t virtualNodesPerNode_;
    mutable std::mutex mutex_;
    std::set<std::pair<uint64_t, std::string>> ring_;
    std::set<std::string> nodes_;
};

}  // namespace common
}  // namespace outpost
