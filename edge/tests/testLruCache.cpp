/// @file testLruCache.cpp
/// @brief Unit tests for outpost::common::LruCache.

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "common/lruCache.h"

using outpost::common::LruCache;

TEST(LruCacheTest, GetOnEmptyReturnsNullopt)
{
    LruCache cache(10);
    EXPECT_FALSE(cache.get("missing").has_value());
}

TEST(LruCacheTest, PutThenGetReturnsValue)
{
    LruCache cache(10);
    cache.put("k1", "v1");
    auto val = cache.get("k1");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "v1");
}

TEST(LruCacheTest, PutOverwritesExistingKey)
{
    LruCache cache(10);
    cache.put("k1", "original");
    cache.put("k1", "updated");
    auto val = cache.get("k1");
    ASSERT_TRUE(val.has_value());
    EXPECT_EQ(val.value(), "updated");
}

TEST(LruCacheTest, EvictsLeastRecentlyUsedWhenFull)
{
    LruCache cache(3);
    cache.put("a", "1");
    cache.put("b", "2");
    cache.put("c", "3");
    // Cache is full: [c, b, a] (c=MRU, a=LRU)
    EXPECT_EQ(cache.size(), 3u);

    cache.put("d", "4");
    // "a" should have been evicted: [d, c, b]
    EXPECT_EQ(cache.size(), 3u);
    EXPECT_FALSE(cache.get("a").has_value());
    EXPECT_TRUE(cache.get("b").has_value());
    EXPECT_TRUE(cache.get("c").has_value());
    EXPECT_TRUE(cache.get("d").has_value());
}

TEST(LruCacheTest, GetPromotesToMRU)
{
    LruCache cache(3);
    cache.put("a", "1");
    cache.put("b", "2");
    cache.put("c", "3");
    // Order: [c, b, a]

    // Access "a" — it becomes MRU
    cache.get("a");
    // Order: [a, c, b]

    // Insert "d" — "b" should be evicted (now LRU)
    cache.put("d", "4");
    EXPECT_FALSE(cache.get("b").has_value());  // evicted
    EXPECT_TRUE(cache.get("a").has_value());   // was promoted
    EXPECT_TRUE(cache.get("c").has_value());
    EXPECT_TRUE(cache.get("d").has_value());
}

TEST(LruCacheTest, PutExistingKeyPromotesToMRU)
{
    LruCache cache(3);
    cache.put("a", "1");
    cache.put("b", "2");
    cache.put("c", "3");
    // Order: [c, b, a]

    // Update "a" — it becomes MRU
    cache.put("a", "updated");
    // Order: [a, c, b]

    cache.put("d", "4");
    // "b" evicted: [d, a, c]
    EXPECT_FALSE(cache.get("b").has_value());
    EXPECT_EQ(cache.get("a").value(), "updated");
}

TEST(LruCacheTest, ContainsDoesNotPromote)
{
    LruCache cache(3);
    cache.put("a", "1");
    cache.put("b", "2");
    cache.put("c", "3");

    // contains() does NOT promote "a"
    EXPECT_TRUE(cache.contains("a"));

    cache.put("d", "4");
    // "a" should still be evicted (contains didn't promote it)
    EXPECT_FALSE(cache.get("a").has_value());
}

TEST(LruCacheTest, RemoveEntry)
{
    LruCache cache(10);
    cache.put("k1", "v1");
    EXPECT_TRUE(cache.remove("k1"));
    EXPECT_FALSE(cache.get("k1").has_value());
    EXPECT_EQ(cache.size(), 0u);
}

TEST(LruCacheTest, RemoveNonexistentReturnsFalse)
{
    LruCache cache(10);
    EXPECT_FALSE(cache.remove("ghost"));
}

TEST(LruCacheTest, ClearResetsEverything)
{
    LruCache cache(10);
    cache.put("a", "1");
    cache.put("b", "2");
    cache.get("a");  // Generate some hits

    cache.clear();
    auto stats = cache.stats();
    EXPECT_EQ(stats.hits, 0u);
    EXPECT_EQ(stats.misses, 0u);
    EXPECT_EQ(cache.size(), 0u);
    EXPECT_FALSE(cache.get("a").has_value());
}

TEST(LruCacheTest, StatsTracking)
{
    LruCache cache(2);
    cache.put("a", "1");   // puts=1
    cache.put("b", "2");   // puts=2
    cache.get("a");        // hit
    cache.get("missing");  // miss
    cache.put("c", "3");   // puts=3, eviction of "b"

    auto stats = cache.stats();
    EXPECT_EQ(stats.hits, 1u);
    EXPECT_EQ(stats.misses, 1u);
    EXPECT_EQ(stats.puts, 3u);
    EXPECT_EQ(stats.evictions, 1u);
    EXPECT_DOUBLE_EQ(stats.hitRate(), 50.0);
}

TEST(LruCacheTest, ZeroCapacityThrows)
{
    EXPECT_THROW(LruCache cache(0), std::invalid_argument);
}

TEST(LruCacheTest, CapacityOneWorks)
{
    LruCache cache(1);
    cache.put("a", "1");
    EXPECT_EQ(cache.get("a").value(), "1");

    cache.put("b", "2");
    EXPECT_FALSE(cache.get("a").has_value());
    EXPECT_EQ(cache.get("b").value(), "2");
}

TEST(LruCacheTest, ConcurrentAccess)
{
    LruCache cache(100);
    constexpr int kThreads = 4;
    constexpr int kOpsPerThread = 500;

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&cache, t]() {
            for (int i = 0; i < kOpsPerThread; ++i)
            {
                std::string key = "t" + std::to_string(t) + "_k" + std::to_string(i % 50);
                cache.put(key, "v" + std::to_string(i));
                cache.get(key);
            }
        });
    }
    for (auto& th : threads)
        th.join();

    // Just verify no crash — exact state depends on scheduling
    EXPECT_LE(cache.size(), 100u);
}

// ── Freshness and versions ──────────────────────────────────────────────

TEST(LruCacheTest, EntryExpiresAfterTtlButStaysAvailableAsStale)
{
    LruCache cache(10, std::chrono::milliseconds(50));
    cache.put("k", "v");
    EXPECT_EQ(cache.get("k").value_or(""), "v");
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    EXPECT_FALSE(cache.get("k").has_value());
    auto stale = cache.lookup("k");
    ASSERT_TRUE(stale.has_value());
    EXPECT_EQ(stale->value, "v");
    EXPECT_FALSE(stale->fresh);
}

TEST(LruCacheTest, ZeroTtlNeverExpires)
{
    LruCache cache(10);
    cache.put("k", "v");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    auto entry = cache.lookup("k");
    ASSERT_TRUE(entry.has_value());
    EXPECT_TRUE(entry->fresh);
}

TEST(LruCacheTest, PutRefreshesAge)
{
    LruCache cache(10, std::chrono::milliseconds(60));
    cache.put("k", "v1");
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    cache.put("k", "v2");
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    EXPECT_EQ(cache.get("k").value_or(""), "v2");
}

TEST(LruCacheTest, PutWithOlderVersionIsIgnored)
{
    LruCache cache(10);
    EXPECT_TRUE(cache.put("k", "newer", 20));
    EXPECT_FALSE(cache.put("k", "older", 10));
    EXPECT_TRUE(cache.put("k", "newest", 30));
    EXPECT_EQ(cache.get("k").value_or(""), "newest");
}

TEST(LruCacheTest, ExpiredLookupCountsAsMiss)
{
    LruCache cache(10, std::chrono::milliseconds(10));
    cache.put("k", "v");
    std::this_thread::sleep_for(std::chrono::milliseconds(25));
    cache.lookup("k");
    EXPECT_EQ(cache.stats().misses, 1u);
    EXPECT_EQ(cache.stats().hits, 0u);
}
