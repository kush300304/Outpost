#pragma once

/// @file edgeServer.h
/// @brief Edge server: cache-aside reads, degraded-mode write queueing and reconciliation.
///
/// HTTP endpoints:
///   GET  /health          → 200 health check
///   GET  /key/:id         → cache-aside read
///   PUT  /key/:id         → versioned write, forwarded to write-api or queued in the edge WAL
///   GET  /debug/stats     → cache, breaker and queue statistics
///
/// A background maintenance thread health-checks write-api every healthCheckInterval, drives the
/// breaker state and, whenever the breaker is CLOSED and writes are queued, drains the queue: each
/// queued write is sent in order with its original version, and only the prefix the origin
/// resolved is removed from the WAL, so writes queued during a drain are never lost.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <httplib.h>

#include "common/lruCache.h"
#include "common/wal.h"
#include "edge/originClient.h"

namespace outpost {
namespace edge {

/// @brief Configuration for an edge server instance.
struct EdgeConfig {
    std::string nodeId = "edge-1";                          ///< Unique node identifier.
    std::string bindAddress = "0.0.0.0";                    ///< Listen address.
    int port = 8081;                                        ///< Listen port; 0 picks a free port.
    std::string writeApiHost = "write-api";                 ///< Write-API hostname.
    int writeApiPort = 9090;                                ///< Write-API port.
    std::size_t cacheCapacity = 1024;                       ///< LRU cache max entries.
    int breakerFailureThreshold = 3;                        ///< Failed checks that open the breaker.
    std::chrono::milliseconds breakerResetTimeout{5000};    ///< OPEN time before a probe.
    std::chrono::milliseconds healthCheckInterval{1000};    ///< Time between health checks.
    std::chrono::milliseconds originTimeout{2000};          ///< Connect/read timeout to write-api.
    std::string walPath;                                    ///< Queue WAL; empty → /data/<nodeId>…
    bool walFsync = true;                                   ///< fsync each queued write.
};

/// @brief Edge server that handles HTTP requests with cache-aside logic.
///
/// Lifecycle: bind() then serve() (blocking) on one thread; stop() from any thread.
class EdgeServer {
public:
    explicit EdgeServer(const EdgeConfig& config);
    ~EdgeServer();

    EdgeServer(const EdgeServer&) = delete;
    EdgeServer& operator=(const EdgeServer&) = delete;

    /// @brief Bind the listening socket.
    /// @return The bound port (a free port when config.port is 0), or -1 on failure.
    int bind();

    /// @brief Start the maintenance thread and serve requests until stop(). Requires bind().
    /// @return true when the server ran and stopped normally.
    bool serve();

    /// @brief Stop serving and join the maintenance thread. Safe to call more than once.
    void stop();

private:
    enum class BreakerState { CLOSED, OPEN, HALF_OPEN };

    void registerRoutes();
    void maintenanceLoop();
    /// @brief Send queued writes in order; discard exactly the prefix the origin resolved.
    void drainQueue();

    EdgeConfig config_;
    httplib::Server server_;
    OriginClient originClient_;
    common::LruCache cache_;
    common::WriteAheadLog writeQueue_;

    std::thread maintenanceThread_;
    std::mutex stopMutex_;
    std::condition_variable stopSignal_;
    bool stopping_ = false;

    std::atomic<uint64_t> totalRequests_{0};
    std::atomic<uint64_t> totalReads_{0};
    std::atomic<uint64_t> totalWrites_{0};
    std::atomic<uint64_t> staleServes_{0};
    std::atomic<uint64_t> queuedWrites_{0};
    std::atomic<uint64_t> reconciledWrites_{0};
    std::atomic<uint64_t> rejectedWrites_{0};
    std::atomic<uint64_t> rateLimited_{0};
    std::atomic<uint64_t> lastVersion_{0};  ///< Last stamped write version (µs since epoch).

    std::atomic<BreakerState> breakerState_{BreakerState::CLOSED};
    std::atomic<int> consecutiveFailures_{0};
    std::atomic<int64_t> breakerOpenedAtMs_{0};
};

}  // namespace edge
}  // namespace outpost
