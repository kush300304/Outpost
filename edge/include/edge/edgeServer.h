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
/// Failure detection: a common::CircuitBreaker guards write-api. A background maintenance thread
/// health-checks write-api whenever breaker.isAllowed() says so: every healthCheckInterval while
/// CLOSED, never while OPEN, and exactly once (the probe) in HALF_OPEN. Request handlers never
/// call isAllowed(); they read the state and record origin successes and failures.
///
/// Reads: a fresh cached key is served as HIT while CLOSED. A missing or expired key is fetched
/// from write-api while CLOSED (MISS) and cached with its version. Whenever the breaker is not
/// CLOSED, or that fetch fails, any cached value (fresh or expired) is served as STALE; with
/// nothing cached the edge answers 503.
///
/// Rate limiting: every /key request consumes a token from its client's bucket (client = first
/// X-Forwarded-For hop, else the peer address); an empty bucket answers 429 with Retry-After: 1.
///
/// Writes are forwarded while CLOSED; otherwise, or when write-api fails, they are queued in the
/// edge WAL (202). Whenever the breaker is CLOSED and writes are queued, the maintenance thread
/// drains the queue: each write is sent in order with its original version, and only the prefix
/// write-api resolved is removed from the WAL, so writes queued during a drain are never lost.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

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
    std::string nodeId = "edge-1";                          ///< Unique node identifier.
    std::string bindAddress = "0.0.0.0";                    ///< Listen address.
    int port = 8081;                                        ///< Listen port; 0 picks a free port.
    std::string writeApiHost = "write-api";                 ///< Write-API hostname.
    int writeApiPort = 9090;                                ///< Write-API port.
    std::size_t cacheCapacity = 1024;                       ///< LRU cache max entries.
    std::chrono::milliseconds cacheTtl{60000};              ///< Entry freshness; 0 = forever.
    double rateLimitPerSec = 100.0;                         ///< Token refill per client per s.
    std::size_t rateLimitBurst = 200;                       ///< Token bucket capacity per client.
    std::size_t maxTrackedClients = 10000;                  ///< Buckets kept before idle eviction.
    int breakerFailureThreshold = 3;                        ///< Failures that open the breaker.
    std::chrono::milliseconds breakerResetTimeout{5000};    ///< OPEN time before a probe.
    std::chrono::milliseconds healthCheckInterval{1000};    ///< Time between health checks.
    std::chrono::milliseconds originTimeout{2000};          ///< Timeout for write-api calls.
    std::string walPath;  ///< Queue WAL; empty means /data/<nodeId>_write_queue.wal.
    bool walFsync = true;                                   ///< fsync each queued write.
};

/// @brief Edge server that handles HTTP requests with cache-aside logic.
///
/// Lifecycle: bind() then serve() (blocking) on one thread; stop() from any thread.
class EdgeServer {
public:
    /// @throws std::invalid_argument when maxTrackedClients or rateLimitBurst is 0, or
    ///         rateLimitPerSec is negative.
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
    /// @brief Maintenance thread body: health checks, breaker state and queue draining.
    void maintenanceLoop();

    /// @brief The token bucket of clientId, created on first use. When maxTrackedClients buckets
    ///        exist, buckets idle for 60 s are evicted, else the least recently seen one.
    std::shared_ptr<common::TokenBucket> bucketFor(const std::string& clientId);

    struct ClientBucket {
        std::shared_ptr<common::TokenBucket> bucket;
        std::chrono::steady_clock::time_point lastSeen;
    };

    EdgeConfig config_;
    httplib::Server server_;
    OriginClient originClient_;
    common::LruCache cache_;
    common::WriteAheadLog writeQueue_;
    common::CircuitBreaker breaker_;

    std::thread maintenanceThread_;
    std::mutex joinMutex_;  ///< Serializes concurrent stop() calls joining the thread.
    std::mutex clientsMutex_;
    std::unordered_map<std::string, ClientBucket> clients_;
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
};

}  // namespace edge
}  // namespace outpost
