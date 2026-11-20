/// @file testTokenBucket.cpp
/// @brief Unit tests for outpost::common::TokenBucket.

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <thread>

#include "common/tokenBucket.h"

using outpost::common::TokenBucket;

TEST(TokenBucketTest, StartsFullAllowsBurstToCapacity)
{
    TokenBucket bucket(10, 1.0);  // 10 capacity, 1 token/sec

    // Should allow exactly 10 requests (full bucket)
    for (int i = 0; i < 10; ++i)
    {
        EXPECT_TRUE(bucket.tryConsume()) << "Request " << i << " should have been allowed";
    }

    // 11th should be throttled
    EXPECT_FALSE(bucket.tryConsume());
}

TEST(TokenBucketTest, ThrottlesWhenEmpty)
{
    TokenBucket bucket(2, 0.5);

    EXPECT_TRUE(bucket.tryConsume());
    EXPECT_TRUE(bucket.tryConsume());
    EXPECT_FALSE(bucket.tryConsume());
    EXPECT_FALSE(bucket.tryConsume());
}

TEST(TokenBucketTest, RefillsOverTime)
{
    TokenBucket bucket(5, 100.0);  // 100 tokens/sec = 1 per 10ms

    // Drain the bucket
    for (int i = 0; i < 5; ++i)
    {
        bucket.tryConsume();
    }
    EXPECT_FALSE(bucket.tryConsume());

    // Wait for refill (100ms should give ~10 tokens, capped at capacity=5)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Should have tokens again
    EXPECT_TRUE(bucket.tryConsume());
}

TEST(TokenBucketTest, DoesNotExceedCapacity)
{
    TokenBucket bucket(3, 1000.0);  // Very fast refill

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Even with fast refill + wait, tokens should cap at capacity=3
    double tokens = bucket.tokensAvailable();
    EXPECT_LE(tokens, 3.0);
}

TEST(TokenBucketTest, TryConsumeMultiple)
{
    TokenBucket bucket(10, 1.0);

    EXPECT_TRUE(bucket.tryConsume(5));   // 5 left
    EXPECT_TRUE(bucket.tryConsume(5));   // 0 left
    EXPECT_FALSE(bucket.tryConsume(1));  // Empty
}

TEST(TokenBucketTest, TryConsumeMoreThanAvailable)
{
    TokenBucket bucket(5, 1.0);

    EXPECT_FALSE(bucket.tryConsume(6));  // Can't consume more than available
    // Bucket should still be full (nothing consumed on failure)
    EXPECT_TRUE(bucket.tryConsume(5));
}

TEST(TokenBucketTest, StatsTracking)
{
    TokenBucket bucket(3, 1.0);

    bucket.tryConsume();  // allowed
    bucket.tryConsume();  // allowed
    bucket.tryConsume();  // allowed
    bucket.tryConsume();  // throttled
    bucket.tryConsume();  // throttled

    EXPECT_EQ(bucket.allowed(), 3u);
    EXPECT_EQ(bucket.throttled(), 2u);
}

TEST(TokenBucketTest, CapacityAndRefillRate)
{
    TokenBucket bucket(42, 3.14);
    EXPECT_EQ(bucket.capacity(), 42u);
    EXPECT_DOUBLE_EQ(bucket.refillRate(), 3.14);
}

TEST(TokenBucketTest, RejectsInvalidConfiguration)
{
    EXPECT_THROW(TokenBucket(0, 1.0), std::invalid_argument);
    EXPECT_THROW(TokenBucket(10, -1.0), std::invalid_argument);
}
