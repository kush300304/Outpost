#include "common/tokenBucket.h"

#include <algorithm>

namespace outpost {
namespace common {

TokenBucket::TokenBucket(std::size_t capacity, double refillRate)
    : capacity_(capacity),
      refillRate_(refillRate),
      tokens_(static_cast<double>(capacity)),  // Start full
      lastRefill_(std::chrono::steady_clock::now()) {
}

void TokenBucket::refill() {
    auto now = std::chrono::steady_clock::now();
    double elapsed =
        std::chrono::duration<double>(now - lastRefill_).count();

    if (elapsed > 0.0) {
        tokens_ = std::min(static_cast<double>(capacity_), tokens_ + elapsed * refillRate_);
        lastRefill_ = now;
    }
}

bool TokenBucket::tryConsume() {
    return tryConsume(1);
}

bool TokenBucket::tryConsume(std::size_t count) {
    std::lock_guard lock(mutex_);
    refill();

    double needed = static_cast<double>(count);
    if (tokens_ >= needed) {
        tokens_ -= needed;
        ++allowed_;
        return true;
    }

    ++throttled_;
    return false;
}

double TokenBucket::tokensAvailable() {
    std::lock_guard lock(mutex_);
    refill();
    return tokens_;
}

uint64_t TokenBucket::allowed() const {
    std::lock_guard lock(mutex_);
    return allowed_;
}

uint64_t TokenBucket::throttled() const {
    std::lock_guard lock(mutex_);
    return throttled_;
}

}  // namespace common
}  // namespace outpost
