/// @file circuitBreaker.cpp
/// @brief Circuit breaker state machine with a single half-open probe.

#include "common/circuitBreaker.h"

namespace outpost {
namespace common {

CircuitBreaker::CircuitBreaker(const CircuitBreakerConfig& config) : config_(config) {
}

std::optional<CircuitBreaker::Transition> CircuitBreaker::setState(BreakerState newState,
                                                                   Clock::time_point now) {
    std::optional<Transition> transition;
    if (state_ != newState) {
        transition = Transition{state_, newState};
        state_ = newState;
    }
    if (newState == BreakerState::OPEN) {
        openedAt_ = now;
    } else if (newState == BreakerState::HALF_OPEN) {
        probeStartedAt_ = now;
    } else {
        consecutiveFailures_ = 0;
    }
    return transition;
}

void CircuitBreaker::notify(const std::optional<Transition>& transition,
                            const TransitionCallback& callback) {
    if (transition && callback) {
        callback(transition->first, transition->second);
    }
}

bool CircuitBreaker::isAllowed() {
    bool allowed = false;
    std::optional<Transition> transition;
    TransitionCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        Clock::time_point now = Clock::now();
        if (state_ == BreakerState::CLOSED) {
            allowed = true;
        } else if (state_ == BreakerState::OPEN) {
            allowed = now - openedAt_ >= config_.resetTimeout;
            if (allowed) {
                transition = setState(BreakerState::HALF_OPEN, now);
            }
        } else {
            allowed = now - probeStartedAt_ >= config_.resetTimeout;
            if (allowed) {
                probeStartedAt_ = now;
            }
        }
        callback = transitionCallback_;
    }
    notify(transition, callback);
    return allowed;
}

void CircuitBreaker::recordSuccess() {
    std::optional<Transition> transition;
    TransitionCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        consecutiveFailures_ = 0;
        if (state_ == BreakerState::HALF_OPEN) {
            transition = setState(BreakerState::CLOSED, Clock::now());
        }
        callback = transitionCallback_;
    }
    notify(transition, callback);
}

void CircuitBreaker::recordFailure() {
    std::optional<Transition> transition;
    TransitionCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++consecutiveFailures_;
        bool trips = (state_ == BreakerState::CLOSED &&
                      consecutiveFailures_ >= config_.failureThreshold) ||
                     state_ == BreakerState::HALF_OPEN;
        if (trips) {
            transition = setState(BreakerState::OPEN, Clock::now());
        }
        callback = transitionCallback_;
    }
    notify(transition, callback);
}

BreakerState CircuitBreaker::state() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

int CircuitBreaker::consecutiveFailures() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return consecutiveFailures_;
}

const CircuitBreakerConfig& CircuitBreaker::config() const {
    return config_;
}

void CircuitBreaker::onTransition(TransitionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    transitionCallback_ = std::move(callback);
}

void CircuitBreaker::forceState(BreakerState newState) {
    std::optional<Transition> transition;
    TransitionCallback callback;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        transition = setState(newState, Clock::now());
        callback = transitionCallback_;
    }
    notify(transition, callback);
}

}  // namespace common
}  // namespace outpost
