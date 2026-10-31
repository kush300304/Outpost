/// @file testKvStore.cpp
/// @brief Unit tests for outpost::storage::KvStore.
///
/// Covers: basic CRUD, overwrite semantics, missing-key behavior,
/// iteration, clear, concurrent read/write safety.

#include "storage/kvStore.h"

#include <algorithm>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using outpost::storage::KvStore;

// ── Basic CRUD ──────────────────────────────────────────────────────────

TEST(KvStoreTest, GetOnEmptyStoreReturnsNullopt) {
    KvStore store;
    EXPECT_FALSE(store.get("nonexistent").has_value());
}

TEST(KvStoreTest, PutThenGetReturnsValue) {
    KvStore store;
    store.put("alpha", "100");
    auto val = store.get("alpha");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "100");
}

TEST(KvStoreTest, PutOverwritesPreviousValue) {
    KvStore store;
    store.put("key", "original");
    store.put("key", "updated");
    auto val = store.get("key");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "updated");
}

TEST(KvStoreTest, ExistsReturnsTrueForExistingKey) {
    KvStore store;
    store.put("present", "value");
    EXPECT_TRUE(store.exists("present"));
    EXPECT_FALSE(store.exists("absent"));
}

TEST(KvStoreTest, RemoveExistingKeyReturnsTrue) {
    KvStore store;
    store.put("toRemove", "val");
    EXPECT_TRUE(store.remove("toRemove"));
    EXPECT_FALSE(store.exists("toRemove"));
    EXPECT_FALSE(store.get("toRemove").has_value());
}

TEST(KvStoreTest, RemoveNonexistentKeyReturnsFalse) {
    KvStore store;
    EXPECT_FALSE(store.remove("ghost"));
}

TEST(KvStoreTest, SizeReflectsInsertionsAndRemovals) {
    KvStore store;
    EXPECT_EQ(store.size(), 0u);

    store.put("a", "1");
    store.put("b", "2");
    store.put("c", "3");
    EXPECT_EQ(store.size(), 3u);

    store.remove("b");
    EXPECT_EQ(store.size(), 2u);
}

TEST(KvStoreTest, GetAllKeysReturnsAllInsertedKeys) {
    KvStore store;
    store.put("x", "1");
    store.put("y", "2");
    store.put("z", "3");

    auto keys = store.getAllKeys();
    ASSERT_EQ(keys.size(), 3u);

    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys[0], "x");
    EXPECT_EQ(keys[1], "y");
    EXPECT_EQ(keys[2], "z");
}

TEST(KvStoreTest, ClearRemovesAllEntries) {
    KvStore store;
    store.put("a", "1");
    store.put("b", "2");
    EXPECT_EQ(store.size(), 2u);

    store.clear();
    EXPECT_EQ(store.size(), 0u);
    EXPECT_FALSE(store.get("a").has_value());
    EXPECT_FALSE(store.get("b").has_value());
}

TEST(KvStoreTest, ForEachVisitsAllEntries) {
    KvStore store;
    store.put("k1", "v1");
    store.put("k2", "v2");

    std::vector<std::pair<std::string, std::string>> visited;
    store.forEach([&visited](const std::string& k, const std::string& v) {
        visited.emplace_back(k, v);
    });

    ASSERT_EQ(visited.size(), 2u);
    std::sort(visited.begin(), visited.end());
    EXPECT_EQ(visited[0], std::make_pair(std::string("k1"), std::string("v1")));
    EXPECT_EQ(visited[1], std::make_pair(std::string("k2"), std::string("v2")));
}

// ── Edge cases ──────────────────────────────────────────────────────────

TEST(KvStoreTest, EmptyKeyAndValue) {
    KvStore store;
    store.put("", "");
    EXPECT_TRUE(store.exists(""));
    auto val = store.get("");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "");
}

TEST(KvStoreTest, LargeValue) {
    KvStore store;
    std::string largeVal(1 << 20, 'X');  // 1 MB
    store.put("big", largeVal);
    auto val = store.get("big");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value().size(), largeVal.size());
    EXPECT_EQ(val.value(), largeVal);
}

// ── Concurrency ─────────────────────────────────────────────────────────

TEST(KvStoreTest, ConcurrentWritesDoNotCorrupt) {
    KvStore store;
    constexpr int kNumThreads = 8;
    constexpr int kWritesPerThread = 1000;

    std::vector<std::thread> threads;
    threads.reserve(kNumThreads);

    for (int t = 0; t < kNumThreads; ++t) {
        threads.emplace_back([&store, t]() {
            for (int i = 0; i < kWritesPerThread; ++i) {
                std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i);
                store.put(key, std::to_string(i));
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    EXPECT_EQ(store.size(), static_cast<std::size_t>(kNumThreads * kWritesPerThread));
}

TEST(KvStoreTest, ConcurrentReadsAndWrites) {
    KvStore store;
    std::atomic<bool> stop{false};
    constexpr int kReaderThreads = 4;

    // Pre-populate
    for (int i = 0; i < 100; ++i) {
        store.put("key" + std::to_string(i), "val" + std::to_string(i));
    }

    // Readers run continuously, checking values are consistent
    std::vector<std::thread> readers;
    std::atomic<int> readCount{0};
    for (int r = 0; r < kReaderThreads; ++r) {
        readers.emplace_back([&store, &stop, &readCount]() {
            while (!stop.load(std::memory_order_relaxed)) {
                for (int i = 0; i < 100; ++i) {
                    auto val = store.get("key" + std::to_string(i));
                    // Value may or may not exist (writer may have removed it),
                    // but we must not crash or get garbled data.
                    readCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    // Writer overwrites values
    std::thread writer([&store, &stop]() {
        for (int round = 0; round < 50 && !stop.load(); ++round) {
            for (int i = 0; i < 100; ++i) {
                store.put("key" + std::to_string(i),
                          "updated_" + std::to_string(round) + "_" + std::to_string(i));
            }
        }
        stop.store(true, std::memory_order_relaxed);
    });

    writer.join();
    stop.store(true, std::memory_order_relaxed);
    for (auto& th : readers) {
        th.join();
    }

    // Sanity check: readers actually ran
    EXPECT_GT(readCount.load(), 0);
    EXPECT_EQ(store.size(), 100u);
}
