/// @file testConsistentHash.cpp
/// @brief Unit tests for outpost::common::ConsistentHashRing.

#include "common/consistentHash.h"

#include <string>
#include <unordered_map>
#include <vector>

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

TEST(ConsistentHashTest, KeysDistributeEvenlyAcrossNodes) {
    ConsistentHashRing ring(150);
    ring.addNode("edge-1");
    ring.addNode("edge-2");
    ring.addNode("edge-3");

    std::unordered_map<std::string, int> distribution;
    constexpr int kNumKeys = 100000;
    for (int i = 0; i < kNumKeys; ++i) {
        ++distribution[ring.getNode("key_" + std::to_string(i))];
    }

    ASSERT_EQ(distribution.size(), 3u);
    for (auto& [node, count] : distribution) {
        // Each node owns 1/3 of the keys within ±20 % (26.7 %–40 %).
        double pct = (static_cast<double>(count) / kNumKeys) * 100.0;
        EXPECT_GT(pct, 26.7) << node << " got too few keys: " << count;
        EXPECT_LT(pct, 40.0) << node << " got too many keys: " << count;
    }
}

TEST(ConsistentHashTest, AddingANodeMovesAboutOneOverNPlusOneOfKeys) {
    constexpr int kNumKeys = 100000;
    for (int existing = 3; existing <= 5; ++existing) {
        ConsistentHashRing ring(150);
        for (int n = 1; n <= existing; ++n) {
            ring.addNode("edge-" + std::to_string(n));
        }
        std::vector<std::string> before(kNumKeys);
        for (int i = 0; i < kNumKeys; ++i) {
            before[i] = ring.getNode("key_" + std::to_string(i));
        }

        std::string added = "edge-" + std::to_string(existing + 1);
        ring.addNode(added);
        int moved = 0;
        int movedBetweenOldNodes = 0;
        for (int i = 0; i < kNumKeys; ++i) {
            std::string after = ring.getNode("key_" + std::to_string(i));
            moved += after != before[i] ? 1 : 0;
            movedBetweenOldNodes += (after != before[i] && after != added) ? 1 : 0;
        }

        double ideal = 1.0 / (existing + 1);
        double fraction = static_cast<double>(moved) / kNumKeys;
        EXPECT_GT(fraction, 0.5 * ideal) << existing << " -> " << existing + 1;
        EXPECT_LT(fraction, 1.5 * ideal) << existing << " -> " << existing + 1;
        EXPECT_EQ(movedBetweenOldNodes, 0) << existing << " -> " << existing + 1;
    }
}

TEST(ConsistentHashTest, RoutingDoesNotDependOnInsertionOrder) {
    ConsistentHashRing forward(150);
    ConsistentHashRing backward(150);
    for (int n = 1; n <= 5; ++n) {
        forward.addNode("edge-" + std::to_string(n));
        backward.addNode("edge-" + std::to_string(6 - n));
    }
    forward.removeNode("edge-3");
    backward.removeNode("edge-3");
    for (int i = 0; i < 10000; ++i) {
        std::string key = "key_" + std::to_string(i);
        ASSERT_EQ(forward.getNode(key), backward.getNode(key)) << key;
    }
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
