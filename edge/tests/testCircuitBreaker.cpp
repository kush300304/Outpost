/// @file testCircuitBreaker.cpp
/// @brief Unit tests for outpost::common::CircuitBreaker.

#include "common/circuitBreaker.h"

#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

using outpost::common::BreakerState;
using outpost::common::CircuitBreaker;
using outpost::common::CircuitBreakerConfig;

TEST(CircuitBreakerTest, StartsInClosedState) {
    CircuitBreaker breaker;
    EXPECT_EQ(breaker.state(), BreakerState::CLOSED);
    EXPECT_EQ(breaker.consecutiveFailures(), 0);
}

TEST(CircuitBreakerTest, ClosedAllowsAllRequests) {
    CircuitBreaker breaker;
    for (int i = 0; i < 100; ++i) {
        EXPECT_TRUE(breaker.isAllowed());
    }
}

TEST(CircuitBreakerTest, ClosedToOpenAfterThreshold) {
    CircuitBreakerConfig config;
    config.failureThreshold = 3;
    CircuitBreaker breaker(config);

    breaker.recordFailure();
    EXPECT_EQ(breaker.state(), BreakerState::CLOSED);
    breaker.recordFailure();
    EXPECT_EQ(breaker.state(), BreakerState::CLOSED);
    breaker.recordFailure();  // 3rd failure → OPEN
    EXPECT_EQ(breaker.state(), BreakerState::OPEN);
}

TEST(CircuitBreakerTest, OpenRejectsRequests) {
    CircuitBreakerConfig config;
    config.failureThreshold = 1;
    config.resetTimeout = std::chrono::seconds(60);  // Long timeout so it stays OPEN
    CircuitBreaker breaker(config);

    breaker.recordFailure();  // → OPEN
    EXPECT_EQ(breaker.state(), BreakerState::OPEN);
    EXPECT_FALSE(breaker.isAllowed());
    EXPECT_FALSE(breaker.isAllowed());
}

TEST(CircuitBreakerTest, SuccessResetFailureCount) {
    CircuitBreakerConfig config;
    config.failureThreshold = 3;
    CircuitBreaker breaker(config);

    breaker.recordFailure();
    breaker.recordFailure();
    EXPECT_EQ(breaker.consecutiveFailures(), 2);

    breaker.recordSuccess();
    EXPECT_EQ(breaker.consecutiveFailures(), 0);
    EXPECT_EQ(breaker.state(), BreakerState::CLOSED);
}

TEST(CircuitBreakerTest, OpenToHalfOpenAfterTimeout) {
    CircuitBreakerConfig config;
    config.failureThreshold = 1;
    config.resetTimeout = std::chrono::milliseconds(100);
    CircuitBreaker breaker(config);

    breaker.recordFailure();  // → OPEN
    EXPECT_EQ(breaker.state(), BreakerState::OPEN);

    // Wait for timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    // Next isAllowed() call should transition to HALF_OPEN and allow
    EXPECT_TRUE(breaker.isAllowed());
    EXPECT_EQ(breaker.state(), BreakerState::HALF_OPEN);
}

TEST(CircuitBreakerTest, HalfOpenSuccessCloses) {
    CircuitBreakerConfig config;
    config.failureThreshold = 1;
    config.resetTimeout = std::chrono::milliseconds(100);
    CircuitBreaker breaker(config);

    breaker.recordFailure();  // → OPEN
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    breaker.isAllowed();  // → HALF_OPEN

    breaker.recordSuccess();  // Probe succeeded → CLOSED
    EXPECT_EQ(breaker.state(), BreakerState::CLOSED);
    EXPECT_TRUE(breaker.isAllowed());
}

TEST(CircuitBreakerTest, HalfOpenFailureReopens) {
    CircuitBreakerConfig config;
    config.failureThreshold = 1;
    config.resetTimeout = std::chrono::milliseconds(100);
    CircuitBreaker breaker(config);

    breaker.recordFailure();  // → OPEN
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    breaker.isAllowed();  // → HALF_OPEN

    breaker.recordFailure();  // Probe failed → back to OPEN
    EXPECT_EQ(breaker.state(), BreakerState::OPEN);
}

TEST(CircuitBreakerTest, HalfOpenLimitsProbes) {
    CircuitBreakerConfig config;
    config.failureThreshold = 1;
    config.resetTimeout = std::chrono::milliseconds(100);
    CircuitBreaker breaker(config);

    breaker.recordFailure();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));

    EXPECT_TRUE(breaker.isAllowed());   // First probe allowed → HALF_OPEN
    EXPECT_FALSE(breaker.isAllowed());  // Second probe blocked
}

TEST(CircuitBreakerTest, TransitionCallback) {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    CircuitBreaker breaker(config);

    std::vector<std::pair<BreakerState, BreakerState>> transitions;
    breaker.onTransition([&transitions](BreakerState from, BreakerState to) {
        transitions.emplace_back(from, to);
    });

    breaker.recordFailure();
    breaker.recordFailure();  // → OPEN

    ASSERT_EQ(transitions.size(), 1u);
    EXPECT_EQ(transitions[0].first, BreakerState::CLOSED);
    EXPECT_EQ(transitions[0].second, BreakerState::OPEN);
}

TEST(CircuitBreakerTest, ForceState) {
    CircuitBreaker breaker;
    breaker.forceState(BreakerState::OPEN);
    EXPECT_EQ(breaker.state(), BreakerState::OPEN);

    breaker.forceState(BreakerState::CLOSED);
    EXPECT_EQ(breaker.state(), BreakerState::CLOSED);
    EXPECT_EQ(breaker.consecutiveFailures(), 0);
}

TEST(CircuitBreakerTest, FullLifecycleClosedOpenHalfOpenClosed) {
    CircuitBreakerConfig config;
    config.failureThreshold = 2;
    config.resetTimeout = std::chrono::milliseconds(100);
    CircuitBreaker breaker(config);

    // 1. CLOSED — normal operation
    EXPECT_TRUE(breaker.isAllowed());
    EXPECT_EQ(breaker.state(), BreakerState::CLOSED);

    // 2. Failures → OPEN
    breaker.recordFailure();
    breaker.recordFailure();
    EXPECT_EQ(breaker.state(), BreakerState::OPEN);
    EXPECT_FALSE(breaker.isAllowed());

    // 3. Wait for timeout → HALF_OPEN
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT_TRUE(breaker.isAllowed());
    EXPECT_EQ(breaker.state(), BreakerState::HALF_OPEN);

    // 4. Probe success → CLOSED
    breaker.recordSuccess();
    EXPECT_EQ(breaker.state(), BreakerState::CLOSED);
    EXPECT_TRUE(breaker.isAllowed());
}

TEST(CircuitBreakerTest, LostProbeIsReadmittedAfterResetTimeout) {
    CircuitBreakerConfig config;
    config.failureThreshold = 1;
    config.resetTimeout = std::chrono::milliseconds(100);
    CircuitBreaker breaker(config);

    breaker.recordFailure();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT_TRUE(breaker.isAllowed());  // probe admitted, its result never recorded
    for (int i = 0; i < 10 && breaker.isAllowed(); ++i) {
        // drain any further admissions so only the lost-probe path remains
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    EXPECT_TRUE(breaker.isAllowed());   // the lost probe is replaced by one new probe
    EXPECT_FALSE(breaker.isAllowed());  // and only one
}

TEST(CircuitBreakerTest, TransitionCallbackMayQueryBreaker) {
    CircuitBreakerConfig config;
    config.failureThreshold = 1;
    auto breaker = std::make_shared<CircuitBreaker>(config);
    auto observed = std::make_shared<std::promise<BreakerState>>();
    breaker->onTransition([breaker, observed](BreakerState, BreakerState) {
        observed->set_value(breaker->state());
    });

    std::thread([breaker] { breaker->recordFailure(); }).detach();
    auto future = observed->get_future();
    ASSERT_EQ(future.wait_for(std::chrono::seconds(1)), std::future_status::ready)
        << "callback deadlocked while querying the breaker";
    EXPECT_EQ(future.get(), BreakerState::OPEN);
}
