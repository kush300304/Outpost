/// @file testShutdownSignal.cpp
/// @brief Unit tests for outpost::common::ShutdownSignal.

#include "common/shutdownSignal.h"

#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <future>

#include <gtest/gtest.h>

using outpost::common::ShutdownSignal;

TEST(ShutdownSignalTest, RunsCallbackOnceWhenTheProcessGetsSigterm)
{
    std::promise<void> called;
    ShutdownSignal shutdown([&called] { called.set_value(); });
    kill(getpid(), SIGTERM);
    EXPECT_EQ(called.get_future().wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_TRUE(shutdown.received());
}

TEST(ShutdownSignalTest, DestroyingWithoutASignalDoesNotCallBackOrHang)
{
    bool called = false;
    {
        ShutdownSignal shutdown([&called] { called = true; });
        EXPECT_FALSE(shutdown.received());
    }
    EXPECT_FALSE(called);
}
