#include "common/circuitBreaker.h"

#include <spdlog/spdlog.h>

namespace outpost {
namespace common {

CircuitBreaker::CircuitBreaker(const CircuitBreakerConfig& config) : config_(config) {
}

bool CircuitBreaker::isAllowed() {
    std::lock_guard lock(mutex_);

    switch (state_) {
        case BreakerState::CLOSED:
            return true;

        case BreakerState::OPEN: {
            // Check if enough time has elapsed to attempt a half-open probe
            auto elapsed = std::chrono::steady_clock::now() - openedAt_;
            auto timeout = std::chrono::seconds(config_.resetTimeoutSec);
            if (elapsed >= timeout) {
                transition(BreakerState::HALF_OPEN);
                halfOpenProbes_ = 0;
                return true;  // Allow the first probe through
            }
            return false;  // Still in cooldown, reject
        }

        case BreakerState::HALF_OPEN: {
            if (halfOpenProbes_ < config_.halfOpenMaxProbes) {
                ++halfOpenProbes_;
                return true;  // Allow probe
            }
            return false;  // Max probes reached, wait for result
        }

        default:
            return false;
    }
}

void CircuitBreaker::recordSuccess() {
    std::lock_guard lock(mutex_);
    consecutiveFailures_ = 0;

    if (state_ == BreakerState::HALF_OPEN) {
        // Probe succeeded — the service is healthy again
        transition(BreakerState::CLOSED);
        halfOpenProbes_ = 0;
    }
}

void CircuitBreaker::recordFailure() {
    std::lock_guard lock(mutex_);
    ++consecutiveFailures_;

    switch (state_) {
        case BreakerState::CLOSED:
            if (consecutiveFailures_ >= config_.failureThreshold) {
                transition(BreakerState::OPEN);
                openedAt_ = std::chrono::steady_clock::now();
            }
            break;

        case BreakerState::HALF_OPEN:
            // Probe failed — back to OPEN with a fresh timeout
            transition(BreakerState::OPEN);
            openedAt_ = std::chrono::steady_clock::now();
            halfOpenProbes_ = 0;
            break;

        case BreakerState::OPEN:
            // Already open, just count failures
            break;
    }
}

BreakerState CircuitBreaker::state() const {
    std::lock_guard lock(mutex_);
    return state_;
}

int CircuitBreaker::consecutiveFailures() const {
    std::lock_guard lock(mutex_);
    return consecutiveFailures_;
}

void CircuitBreaker::onTransition(TransitionCallback callback) {
    std::lock_guard lock(mutex_);
    transitionCallback_ = std::move(callback);
}

void CircuitBreaker::forceState(BreakerState newState) {
    std::lock_guard lock(mutex_);
    transition(newState);
    if (newState == BreakerState::OPEN) {
        openedAt_ = std::chrono::steady_clock::now();
    }
    if (newState == BreakerState::CLOSED) {
        consecutiveFailures_ = 0;
    }
}

void CircuitBreaker::transition(BreakerState newState) {
    // Caller must hold mutex_
    if (state_ == newState) return;

    BreakerState oldState = state_;
    state_ = newState;

    spdlog::info("CircuitBreaker: {} -> {}", breakerStateToString(oldState),
                 breakerStateToString(newState));

    if (transitionCallback_) {
        transitionCallback_(oldState, newState);
    }
}

}  // namespace common
}  // namespace outpost
