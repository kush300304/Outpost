/// @file shutdownSignal.cpp
/// @brief SIGINT/SIGTERM handling with a dedicated sigwait() thread.

#include "common/shutdownSignal.h"

#include <pthread.h>

#include <utility>

namespace outpost
{
namespace common
{

ShutdownSignal::ShutdownSignal(std::function<void()> onSignal) : onSignal_(std::move(onSignal))
{
    sigemptyset(&signals_);
    sigaddset(&signals_, SIGINT);
    sigaddset(&signals_, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &signals_, &previousMask_);
    waiter_ = std::thread([this] {
        int signal = 0;
        sigwait(&signals_, &signal);
        if (!closing_.load())
        {
            received_.store(true);
            onSignal_();
        }
    });
}

ShutdownSignal::~ShutdownSignal()
{
    closing_.store(true);
    if (!received_.load())
    {
        pthread_kill(waiter_.native_handle(), SIGTERM);
    }
    waiter_.join();
    pthread_sigmask(SIG_SETMASK, &previousMask_, nullptr);
}

bool ShutdownSignal::received() const
{
    return received_.load();
}

}  // namespace common
}  // namespace outpost
