#pragma once

/// @file circuitBreaker.h
/// @brief Circuit breaker state machine (Closed → Open → Half-Open → Closed).
///
/// Implements the circuit breaker pattern (Fowler / Netflix Hystrix):
///
///   CLOSED    — normal operation, requests flow through.
///               After `failureThreshold` consecutive failures → OPEN.
///
///   OPEN      — requests are rejected immediately (fail fast).
///               After `resetTimeout` elapses → HALF_OPEN.
///
///   HALF_OPEN — one probe request is allowed through.
///               If it succeeds → CLOSED.
///               If it fails → back to OPEN.
///
/// This state machine drives the edge's degraded mode: when OPEN, edges
/// serve stale cache and queue writes locally instead of calling the origin.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace outpost {
namespace common {

/// @brief Circuit breaker states.
enum class BreakerState : uint8_t {
    CLOSED = 0,
    OPEN = 1,
    HALF_OPEN = 2,
};

/// @brief Convert BreakerState to a human-readable string.
inline const char* breakerStateToString(BreakerState state) {
    switch (state) {
        case BreakerState::CLOSED: return "CLOSED";
        case BreakerState::OPEN: return "OPEN";
        case BreakerState::HALF_OPEN: return "HALF_OPEN";
        default: return "UNKNOWN";
    }
}

/// @brief Configuration for a circuit breaker instance.
struct CircuitBreakerConfig {
    int failureThreshold = 5;        ///< Consecutive failures to trip the breaker.
    int resetTimeoutSec = 30;        ///< Seconds to wait before half-open probe.
    int halfOpenMaxProbes = 1;       ///< Number of probes allowed in half-open state.
};

/// @brief Thread-safe circuit breaker state machine.
class CircuitBreaker {
public:
    explicit CircuitBreaker(const CircuitBreakerConfig& config = {});

    /// @brief Check if a request should be allowed through.
    ///
    /// In CLOSED state: always returns true.
    /// In OPEN state: returns false (fail fast). If enough time has passed,
    ///   transitions to HALF_OPEN.
    /// In HALF_OPEN state: returns true for up to `halfOpenMaxProbes` probes.
    bool isAllowed();

    /// @brief Record a successful request/health check.
    ///
    /// Resets consecutive failure count. If in HALF_OPEN, transitions to CLOSED.
    void recordSuccess();

    /// @brief Record a failed request/health check.
    ///
    /// Increments consecutive failure count. If threshold is reached in CLOSED,
    /// transitions to OPEN. If in HALF_OPEN, transitions back to OPEN.
    void recordFailure();

    /// @brief Return the current state of the breaker.
    BreakerState state() const;

    /// @brief Return the current consecutive failure count.
    int consecutiveFailures() const;

    /// @brief Register a callback for state transitions (for logging/metrics).
    using TransitionCallback = std::function<void(BreakerState from, BreakerState to)>;
    void onTransition(TransitionCallback callback);

    /// @brief Force the breaker to a specific state (for testing).
    void forceState(BreakerState newState);

private:
    void transition(BreakerState newState);

    CircuitBreakerConfig config_;
    mutable std::mutex mutex_;
    BreakerState state_ = BreakerState::CLOSED;
    int consecutiveFailures_ = 0;
    std::chrono::steady_clock::time_point openedAt_;
    int halfOpenProbes_ = 0;
    TransitionCallback transitionCallback_;
};

}  // namespace common
}  // namespace outpost
