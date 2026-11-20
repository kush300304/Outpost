#pragma once

/// @file tokenBucket.h
/// @brief Token bucket rate limiter.
///
/// Classic token bucket algorithm: a bucket holds up to `capacity` tokens.
/// Tokens are added at `refillRate` tokens per second. Each request consumes
/// one token. If the bucket is empty, the request is throttled.
///
/// Used per-client on edge nodes to prevent any single client from
/// overwhelming the system.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace outpost
{
namespace common
{

/// @brief Thread-safe token bucket rate limiter.
class TokenBucket
{
public:
    /// @param capacity    Maximum tokens the bucket can hold (burst size); must be > 0.
    /// @param refillRate  Tokens added per second; must be >= 0.
    /// @throws std::invalid_argument for an invalid capacity or refill rate.
    TokenBucket(std::size_t capacity, double refillRate);

    /// @brief Try to consume one token.
    /// @return true if a token was available and consumed, false if throttled.
    bool tryConsume();

    /// @brief Try to consume N tokens.
    /// @param count Number of tokens to consume.
    /// @return true if enough tokens were available and consumed.
    bool tryConsume(std::size_t count);

    /// @brief Return the current number of available tokens (approximate).
    double tokensAvailable();

    /// @brief Return the bucket capacity (max burst).
    std::size_t capacity() const
    {
        return capacity_;
    }

    /// @brief Return the refill rate (tokens/second).
    double refillRate() const
    {
        return refillRate_;
    }

    /// @brief Return the total number of requests that were allowed.
    uint64_t allowed() const;

    /// @brief Return the total number of requests that were throttled.
    uint64_t throttled() const;

private:
    /// @brief Refill tokens based on elapsed time since last refill.
    void refill();

    std::size_t capacity_;
    double refillRate_;
    double tokens_;
    std::chrono::steady_clock::time_point lastRefill_;
    mutable std::mutex mutex_;
    uint64_t allowed_ = 0;
    uint64_t throttled_ = 0;
};

}  // namespace common
}  // namespace outpost
