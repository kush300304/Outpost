#pragma once

/// @file circuitBreaker.h
/// @brief Circuit breaker state machine (CLOSED → OPEN → HALF_OPEN → CLOSED).
///
///   CLOSED     requests flow; failureThreshold consecutive failures → OPEN.
///   OPEN       isAllowed() is false until resetTimeout has elapsed; the first isAllowed()
///              after that moves to HALF_OPEN and admits exactly one probe.
///   HALF_OPEN  no further probe is admitted while the probe is outstanding. recordSuccess()
///              → CLOSED, recordFailure() → OPEN with a fresh timeout. A probe whose result
///              is never recorded counts as lost after resetTimeout; one new probe is admitted.
///
/// Thread safety: all methods are serialized by one mutex. The transition callback runs after the
/// mutex is released, so it may call back into the breaker.

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace outpost {
namespace common {

/// @brief Circuit breaker states.
enum class BreakerState : uint8_t {
    CLOSED = 0,
    OPEN = 1,
    HALF_OPEN = 2,
};

/// @brief Human-readable name of a state ("CLOSED", "OPEN", "HALF_OPEN").
inline const char* breakerStateToString(BreakerState state) {
    const char* name = "HALF_OPEN";
    if (state == BreakerState::CLOSED) {
        name = "CLOSED";
    } else if (state == BreakerState::OPEN) {
        name = "OPEN";
    }
    return name;
}

/// @brief Configuration for a circuit breaker instance.
struct CircuitBreakerConfig {
    int failureThreshold = 5;                       ///< Consecutive failures that open it.
    std::chrono::milliseconds resetTimeout{30000};  ///< OPEN time before a probe; probe timeout.
};

/// @brief Thread-safe circuit breaker with a single half-open probe.
class CircuitBreaker {
public:
    using TransitionCallback = std::function<void(BreakerState from, BreakerState to)>;

    explicit CircuitBreaker(const CircuitBreakerConfig& config = {});

    /// @brief Whether a call may go to the protected service now.
    ///
    /// CLOSED: always true. OPEN: false until resetTimeout elapses, then moves to HALF_OPEN and
    /// returns true once (the probe). HALF_OPEN: false while the probe is outstanding; true once
    /// more if the probe has been outstanding for resetTimeout.
    bool isAllowed();

    /// @brief Record a successful call. Resets the failure count; HALF_OPEN → CLOSED.
    void recordSuccess();

    /// @brief Record a failed call. CLOSED → OPEN at the threshold; HALF_OPEN → OPEN.
    void recordFailure();

    /// @brief Current state.
    BreakerState state() const;

    /// @brief Current consecutive failure count.
    int consecutiveFailures() const;

    /// @brief The configuration this breaker was built with.
    const CircuitBreakerConfig& config() const;

    /// @brief Register the callback invoked after every state change.
    void onTransition(TransitionCallback callback);

    /// @brief Force a state (testing and operations).
    void forceState(BreakerState newState);

private:
    using Clock = std::chrono::steady_clock;
    using Transition = std::pair<BreakerState, BreakerState>;

    /// @brief Change state under the lock; returns the transition when the state changed.
    std::optional<Transition> setState(BreakerState newState, Clock::time_point now);

    /// @brief Invoke the callback for a transition; must be called without the lock held.
    void notify(const std::optional<Transition>& transition, const TransitionCallback& callback);

    const CircuitBreakerConfig config_;
    mutable std::mutex mutex_;
    BreakerState state_ = BreakerState::CLOSED;
    int consecutiveFailures_ = 0;
    Clock::time_point openedAt_;
    Clock::time_point probeStartedAt_;
    TransitionCallback transitionCallback_;
};

}  // namespace common
}  // namespace outpost
