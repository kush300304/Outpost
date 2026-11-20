#pragma once

/// @file shutdownSignal.h
/// @brief Graceful shutdown on SIGINT and SIGTERM.
///
/// Construct one ShutdownSignal in main() before any other thread starts. It blocks SIGINT and
/// SIGTERM in the calling thread, so every thread created afterwards inherits the block, and waits
/// for them with sigwait() on its own thread. When a signal arrives, onSignal runs once on that
/// thread (typically server.stop()), so main() can return normally and destructors run. This
/// matters in containers, where a process running as PID 1 ignores SIGTERM unless it handles it.

#include <signal.h>

#include <atomic>
#include <functional>
#include <thread>

namespace outpost
{
namespace common
{

/// @brief Runs a callback once when the process receives SIGINT or SIGTERM.
class ShutdownSignal
{
public:
    /// @param onSignal Called on the waiting thread when SIGINT or SIGTERM arrives.
    explicit ShutdownSignal(std::function<void()> onSignal);

    /// @brief Stops waiting (when no signal arrived), joins the thread and restores the mask.
    ~ShutdownSignal();

    ShutdownSignal(const ShutdownSignal&) = delete;
    ShutdownSignal& operator=(const ShutdownSignal&) = delete;

    /// @brief Whether a signal has been received.
    bool received() const;

private:
    std::function<void()> onSignal_;
    sigset_t signals_;
    sigset_t previousMask_;
    std::atomic<bool> received_{false};
    std::atomic<bool> closing_{false};
    std::thread waiter_;
};

}  // namespace common
}  // namespace outpost
