/// @file testConsistentHash.cpp
/// @brief Unit tests for outpost::common::ConsistentHashRing.

#include "common/consistentHash.h"

#include <cmath>
#include <set>
#include <string>
#include <unordered_map>

#include <gtest/gtest.h>

using outpost::common::ConsistentHashRing;

TEST(ConsistentHashTest, EmptyRingReturnsEmptyString) {
    ConsistentHashRing ring;
    EXPECT_EQ(ring.getNode("anykey"), "");
    EXPECT_EQ(ring.nodeCount(), 0u);
}

TEST(ConsistentHashTest, SingleNodeGetsAllKeys) {
    ConsistentHashRing ring;
    ring.addNode("edge-1");

    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(ring.getNode("key" + std::to_string(i)), "edge-1");
    }
}

TEST(ConsistentHashTest, SameKeyAlwaysMapsToSameNode) {
    ConsistentHashRing ring;
    ring.addNode("edge-1");
    ring.addNode("edge-2");
    ring.addNode("edge-3");

    // Run the same key 100 times — must always go to the same node
    for (int round = 0; round < 100; ++round) {
        std::string node1 = ring.getNode("consistentKey");
        std::string node2 = ring.getNode("consistentKey");
        EXPECT_EQ(node1, node2);
    }
}

TEST(ConsistentHashTest, KeysDistributeAcrossNodes) {
    ConsistentHashRing ring(150);
    ring.addNode("edge-1");
    ring.addNode("edge-2");
    ring.addNode("edge-3");

    std::unordered_map<std::string, int> distribution;
    constexpr int kNumKeys = 10000;

    for (int i = 0; i < kNumKeys; ++i) {
        std::string node = ring.getNode("key_" + std::to_string(i));
        ++distribution[node];
    }

    // All 3 nodes should get some keys
    EXPECT_EQ(distribution.size(), 3u);
    for (auto& [node, count] : distribution) {
        // Each node should get roughly 1/3 of keys.
        // With 150 virtual nodes, expect decent uniformity.
        // Allow wide range: between 15% and 50% of total.
        double pct = (static_cast<double>(count) / kNumKeys) * 100.0;
        EXPECT_GT(pct, 15.0) << node << " got too few keys: " << count;
        EXPECT_LT(pct, 50.0) << node << " got too many keys: " << count;
    }
}

TEST(ConsistentHashTest, AddNodeRemapsApproximatelyOneNthOfKeys) {
    ConsistentHashRing ring(150);
    ring.addNode("edge-1");
    ring.addNode("edge-2");
    ring.addNode("edge-3");

    constexpr int kNumKeys = 10000;

    // Record mapping before adding a 4th node
    std::unordered_map<std::string, std::string> before;
    for (int i = 0; i < kNumKeys; ++i) {
        std::string key = "key_" + std::to_string(i);
        before[key] = ring.getNode(key);
    }

    // Add a 4th node
    ring.addNode("edge-4");

    int remapped = 0;
    for (int i = 0; i < kNumKeys; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::string after = ring.getNode(key);
        if (after != before[key]) {
            ++remapped;
        }
    }

    // With 4 nodes, ~1/4 of keys should remap (25%).
    // Allow range: 10% to 45% to account for variance.
    double remapPct = (static_cast<double>(remapped) / kNumKeys) * 100.0;
    EXPECT_GT(remapPct, 10.0) << "Too few keys remapped: " << remapped;
    EXPECT_LT(remapPct, 45.0) << "Too many keys remapped: " << remapped;
}

TEST(ConsistentHashTest, RemoveNodeRemapsOnlyItsKeys) {
    ConsistentHashRing ring(150);
    ring.addNode("edge-1");
    ring.addNode("edge-2");
    ring.addNode("edge-3");

    constexpr int kNumKeys = 10000;

    std::unordered_map<std::string, std::string> before;
    for (int i = 0; i < kNumKeys; ++i) {
        std::string key = "key_" + std::to_string(i);
        before[key] = ring.getNode(key);
    }

    ring.removeNode("edge-2");

    int remapped = 0;
    for (int i = 0; i < kNumKeys; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::string after = ring.getNode(key);
        if (after != before[key]) {
            ++remapped;
            // Keys that move should have been on edge-2 before
            EXPECT_EQ(before[key], "edge-2")
                << "Key '" << key << "' moved from " << before[key] << " to " << after
                << " but wasn't on the removed node";
        }
    }

    // Only edge-2's keys should remap (~33%)
    EXPECT_GT(remapped, 0);
}

TEST(ConsistentHashTest, AddNodeIdempotent) {
    ConsistentHashRing ring;
    ring.addNode("edge-1");
    ring.addNode("edge-1");  // Duplicate
    EXPECT_EQ(ring.nodeCount(), 1u);
}

TEST(ConsistentHashTest, RemoveNonexistentNodeIsSafe) {
    ConsistentHashRing ring;
    ring.addNode("edge-1");
    ring.removeNode("ghost");
    EXPECT_EQ(ring.nodeCount(), 1u);
}

TEST(ConsistentHashTest, HasNodeAndGetNodes) {
    ConsistentHashRing ring;
    ring.addNode("edge-1");
    ring.addNode("edge-2");

    EXPECT_TRUE(ring.hasNode("edge-1"));
    EXPECT_TRUE(ring.hasNode("edge-2"));
    EXPECT_FALSE(ring.hasNode("edge-3"));

    auto nodes = ring.getNodes();
    EXPECT_EQ(nodes.size(), 2u);
}

TEST(ConsistentHashTest, HashDeterministic) {
    auto h1 = ConsistentHashRing::hash("test_key");
    auto h2 = ConsistentHashRing::hash("test_key");
    EXPECT_EQ(h1, h2);

    auto h3 = ConsistentHashRing::hash("different_key");
    EXPECT_NE(h1, h3);
}
