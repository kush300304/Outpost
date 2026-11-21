/// @file testKvStore.cpp
/// @brief Unit tests for outpost::storage::KvStore.
///
/// Covers: last-writer-wins outcomes (applied / duplicate / superseded), the writer-id tie-break,
/// durability across restart, idempotent replay of a log that contains repeated or older records,
/// and concurrent access.

#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "common/wal.h"
#include "storage/kvStore.h"

using outpost::common::WalEntry;
using outpost::common::WalOperation;
using outpost::common::WriteAheadLog;
using outpost::storage::KvStore;
using outpost::storage::PutOutcome;

namespace fs = std::filesystem;

/// @brief Unique WAL path per test, removed afterwards.
class KvStoreDurableTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        path_ = (fs::temp_directory_path() /
                 ("outpost_kv_" + std::to_string(::getpid()) + "_" +
                  ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".wal"))
                    .string();
        fs::remove(path_);
    }
    void TearDown() override
    {
        std::error_code ec;
        fs::remove(path_, ec);
    }
    std::string path_;
};

// ── Last-writer-wins ────────────────────────────────────────────────────

TEST(KvStoreTest, GetOnEmptyStoreReturnsNullopt)
{
    KvStore store;
    EXPECT_FALSE(store.get("nonexistent").has_value());
}

TEST(KvStoreTest, FirstPutIsAppliedWithItsVersion)
{
    KvStore store;
    EXPECT_EQ(store.put("alpha", "100", 10, "edge-1"), PutOutcome::APPLIED);
    auto stored = store.get("alpha");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->value, "100");
    EXPECT_EQ(stored->version, 10u);
    EXPECT_EQ(stored->writerId, "edge-1");
}

TEST(KvStoreTest, NewerVersionOverwrites)
{
    KvStore store;
    store.put("key", "original", 10, "edge-1");
    EXPECT_EQ(store.put("key", "updated", 11, "edge-2"), PutOutcome::APPLIED);
    EXPECT_EQ(store.get("key")->value, "updated");
}

TEST(KvStoreTest, SameVersionAndWriterIsDuplicate)
{
    KvStore store;
    store.put("key", "v", 10, "edge-1");
    EXPECT_EQ(store.put("key", "v", 10, "edge-1"), PutOutcome::DUPLICATE);
    EXPECT_EQ(store.get("key")->value, "v");
    EXPECT_EQ(store.counters().applied, 1u);
    EXPECT_EQ(store.counters().duplicates, 1u);
}

TEST(KvStoreTest, OlderVersionNeverOverwritesNewerData)
{
    KvStore store;
    store.put("key", "newer", 20, "edge-1");
    EXPECT_EQ(store.put("key", "older", 10, "edge-2"), PutOutcome::SUPERSEDED);
    EXPECT_EQ(store.get("key")->value, "newer");
    EXPECT_EQ(store.counters().superseded, 1u);
}

TEST(KvStoreTest, EqualVersionTieBreaksOnWriterId)
{
    KvStore store;
    store.put("key", "from-a", 10, "edge-a");
    EXPECT_EQ(store.put("key", "from-b", 10, "edge-b"), PutOutcome::APPLIED);
    EXPECT_EQ(store.put("key", "from-a-again", 10, "edge-a"), PutOutcome::SUPERSEDED);
    EXPECT_EQ(store.get("key")->value, "from-b");
}

TEST(KvStoreTest, GetAllKeysReturnsAllInsertedKeys)
{
    KvStore store;
    store.put("x", "1", 1, "w");
    store.put("y", "2", 1, "w");
    store.put("z", "3", 1, "w");
    auto keys = store.getAllKeys();
    std::sort(keys.begin(), keys.end());
    EXPECT_EQ(keys, (std::vector<std::string>{"x", "y", "z"}));
    EXPECT_EQ(store.size(), 3u);
}

TEST(KvStoreTest, ForEachVisitsAllEntries)
{
    KvStore store;
    store.put("k1", "v1", 1, "w");
    store.put("k2", "v2", 1, "w");
    std::vector<std::pair<std::string, std::string>> visited;
    store.forEach([&visited](const std::string& key, const std::string& value) {
        visited.emplace_back(key, value);
    });
    std::sort(visited.begin(), visited.end());
    ASSERT_EQ(visited.size(), 2u);
    EXPECT_EQ(visited[0], std::make_pair(std::string("k1"), std::string("v1")));
    EXPECT_EQ(visited[1], std::make_pair(std::string("k2"), std::string("v2")));
}

TEST(KvStoreTest, EmptyKeyAndLargeValue)
{
    KvStore store;
    std::string large(1 << 20, 'X');
    store.put("", "", 1, "w");
    store.put("big", large, 1, "w");
    EXPECT_TRUE(store.exists(""));
    EXPECT_EQ(store.get("big")->value, large);
}

// ── Durability and replay ───────────────────────────────────────────────

TEST_F(KvStoreDurableTest, StateSurvivesRestart)
{
    {
        KvStore store(path_, false);
        store.put("a", "1", 1, "edge-1");
        store.put("a", "2", 2, "edge-1");
        store.put("b", "x", 5, "edge-2");
    }
    KvStore reopened(path_, false);
    EXPECT_EQ(reopened.size(), 2u);
    EXPECT_EQ(reopened.get("a")->value, "2");
    EXPECT_EQ(reopened.get("a")->version, 2u);
    EXPECT_EQ(reopened.get("b")->writerId, "edge-2");
    EXPECT_EQ(reopened.put("a", "2", 2, "edge-1"), PutOutcome::DUPLICATE);
}

TEST_F(KvStoreDurableTest, OnlyAppliedWritesAreLogged)
{
    {
        KvStore store(path_, false);
        store.put("k", "new", 20, "edge-1");
        store.put("k", "new", 20, "edge-1");  // duplicate
        store.put("k", "old", 10, "edge-1");  // superseded
    }
    WriteAheadLog wal(path_, false);
    EXPECT_EQ(wal.size(), 1u);
}

TEST_F(KvStoreDurableTest, ReplayIsIdempotentWhenLogRepeatsOrReordersRecords)
{
    {
        WriteAheadLog wal(path_, false);
        WalEntry newer{WalOperation::PUT, "k", "newer", 20, "edge-1"};
        WalEntry older{WalOperation::PUT, "k", "older", 10, "edge-2"};
        wal.append(newer);
        wal.append(newer);
        wal.append(older);
    }
    KvStore first(path_, false);
    EXPECT_EQ(first.get("k")->value, "newer");
    EXPECT_EQ(first.size(), 1u);
}

// ── Concurrency ─────────────────────────────────────────────────────────

TEST(KvStoreTest, ConcurrentWritesDoNotCorrupt)
{
    KvStore store;
    constexpr int kNumThreads = 8;
    constexpr int kWritesPerThread = 1000;
    std::vector<std::thread> threads;
    for (int t = 0; t < kNumThreads; ++t)
    {
        threads.emplace_back([&store, t]() {
            for (int i = 0; i < kWritesPerThread; ++i)
            {
                store.put("t" + std::to_string(t) + "_k" + std::to_string(i), std::to_string(i),
                          static_cast<uint64_t>(i + 1), "w" + std::to_string(t));
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    EXPECT_EQ(store.size(), static_cast<std::size_t>(kNumThreads * kWritesPerThread));
    EXPECT_EQ(store.counters().applied, static_cast<uint64_t>(kNumThreads * kWritesPerThread));
}

TEST(KvStoreTest, ConcurrentWritersConvergeOnHighestVersion)
{
    KvStore store;
    constexpr int kWriters = 4;
    constexpr int kVersions = 500;
    std::vector<std::thread> threads;
    for (int t = 0; t < kWriters; ++t)
    {
        threads.emplace_back([&store, t]() {
            for (int v = 1; v <= kVersions; ++v)
            {
                store.put("shared", "v" + std::to_string(v) + "-w" + std::to_string(t),
                          static_cast<uint64_t>(v), "w" + std::to_string(t));
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    auto stored = store.get("shared");
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->version, static_cast<uint64_t>(kVersions));
    EXPECT_EQ(stored->writerId, "w" + std::to_string(kWriters - 1));
}

TEST_F(KvStoreDurableTest, ConcurrentDurablePutsShareFsyncsAndConverge)
{
    constexpr int kThreads = 8;
    constexpr int kPutsPerThread = 50;
    {
        KvStore store(path_, true);
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&store, t]() {
                for (int i = 1; i <= kPutsPerThread; ++i)
                {
                    store.put("key-" + std::to_string(i % 10), "v" + std::to_string(i),
                              static_cast<uint64_t>(i), "w" + std::to_string(t));
                }
            });
        }
        for (auto& thread : threads)
        {
            thread.join();
        }
        auto stats = store.walStats();
        EXPECT_LT(stats.fsyncs, stats.appends) << "durable puts did not share fsyncs";
        auto stored = store.get("key-0");
        ASSERT_TRUE(stored.has_value());
        EXPECT_EQ(stored->version, 50u);
        EXPECT_EQ(stored->writerId, "w" + std::to_string(kThreads - 1));
    }
    KvStore reopened(path_, true);
    EXPECT_EQ(reopened.size(), 10u);
    EXPECT_EQ(reopened.get("key-0")->version, 50u);
    EXPECT_EQ(reopened.get("key-0")->writerId, "w" + std::to_string(kThreads - 1));
}
