#pragma once

/// @file edgeServer.h
/// @brief Edge server — HTTP handlers with cache-aside, rate limiting, and circuit breaker.
///
/// Orchestrates all edge-node concerns:
///   1. Rate limiting (token bucket per client IP)
///   2. Cache lookup (LRU cache)
///   3. Circuit breaker check
///   4. Origin fetch on cache miss (via OriginClient)
///   5. Stale-cache serving during degraded mode
///   6. Local write queueing during degraded mode (WAL)
///   7. Reconciliation when breaker closes
///
/// HTTP Endpoints:
///   GET  /health          → 200 health check
///   GET  /key/:id         → cache-aside read
///   PUT  /key/:id         → write (or queue if breaker open)
///   GET  /debug/stats     → cache stats, breaker state, rate limiter info

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include <httplib.h>

#include "common/circuitBreaker.h"
#include "common/lruCache.h"
#include "common/tokenBucket.h"
#include "common/wal.h"
#include "edge/originClient.h"

namespace outpost {
namespace edge {

/// @brief Configuration for an edge server instance.
struct EdgeConfig {
    std::string nodeId = "edge-1";       ///< Unique node identifier.
    int port = 8080;                      ///< HTTP listen port.
    std::string writeApiHost = "write-api";  ///< Write-API hostname.
    int writeApiPort = 8090;              ///< Write-API port.
    std::size_t cacheCapacity = 1024;     ///< LRU cache max entries.
    double rateLimitPerSec = 100.0;       ///< Token bucket refill rate (tokens/sec).
    std::size_t rateLimitBurst = 150;     ///< Token bucket max burst.
    int breakerFailureThreshold = 5;      ///< Consecutive failures to open breaker.
    int breakerResetTimeoutSec = 30;      ///< Seconds before half-open attempt.
    int healthCheckIntervalSec = 5;       ///< Seconds between health checks.
    std::string walPath = "";             ///< WAL path for degraded-mode write queue.
};

/// @brief Edge server that handles HTTP requests with cache-aside logic.
///
/// Call run() to start the server (blocks). Call stop() from another thread
/// to shut it down gracefully.
class EdgeServer {
public:
    explicit EdgeServer(const EdgeConfig& config);
    ~EdgeServer();

    // Non-copyable, non-movable.
    EdgeServer(const EdgeServer&) = delete;
    EdgeServer& operator=(const EdgeServer&) = delete;

    /// @brief Start the HTTP server (blocks until stop() is called).
    void run();

    /// @brief Stop the HTTP server and health-check thread gracefully.
    void stop();

private:
    /// @brief Register all HTTP route handlers on the server.
    void registerRoutes();

    /// @brief Background thread that periodically health-checks the origin.
    void healthCheckLoop();

    /// @brief Drain the edge-local WAL, replaying queued writes to origin.
    void reconcileQueuedWrites();

    EdgeConfig config_;
    httplib::Server server_;
    OriginClient originClient_;
    common::LruCache cache_;

    // Degraded-mode write queue (edge-local WAL for writes when breaker is open).
    std::unique_ptr<common::WriteAheadLog> writeQueue_;

    // Health-check background thread
    std::thread healthCheckThread_;
    std::atomic<bool> running_{false};

    // Request counters
    std::atomic<uint64_t> totalRequests_{0};
    std::atomic<uint64_t> totalReads_{0};
    std::atomic<uint64_t> totalWrites_{0};
    std::atomic<uint64_t> staleServes_{0};
    std::atomic<uint64_t> queuedWrites_{0};
    std::atomic<uint64_t> reconciledWrites_{0};
    std::atomic<uint64_t> rateLimited_{0};

    // Circuit breaker state (simplified for now — full CircuitBreaker class in Phase 3)
    enum class BreakerState { CLOSED, OPEN, HALF_OPEN };
    std::atomic<BreakerState> breakerState_{BreakerState::CLOSED};
    std::atomic<int> consecutiveFailures_{0};
    std::atomic<int64_t> breakerOpenedAtMs_{0};
};

}  // namespace edge
}  // namespace outpost
