#pragma once

/// @file routerServer.h
/// @brief Stateless consistent-hash router in front of the edge nodes.
///
/// HTTP endpoints:
///   GET  /health            → 200 with the number of edges currently on the ring
///   GET  /key/:id           → forwarded to the edge that owns the key
///   PUT  /key/:id           → forwarded to the edge that owns the key
///   POST /purge/key/:id     → POST /purge/key/:id on every configured edge; 200 with each edge's
///                             answer when all answered, 502 (complete: false) when one did not
///   GET  /debug/route/:id   → {"key","node"}: the edge that owns the key right now
///   GET  /debug/stats       → per-edge health, ring membership and routed counts
///
/// Routing: a common::ConsistentHashRing holds the edges that are currently reachable. The owner
/// of a key receives the request with the key percent-encoded, the client appended to
/// X-Forwarded-For, the client's If-None-Match and Cache-Control, and the body for PUT; its status,
/// body and X-Cache, X-Node, X-Write-Mode, Retry-After, ETag, Last-Modified, Age and Cache-Control
/// headers are returned unchanged (a 304 has no body).
///
/// Membership: every configured edge starts on the ring. A background thread checks each edge's
/// /health every healthCheckInterval, removes an edge after unhealthyThreshold consecutive
/// failures and adds it back after one success, so only that edge's keys move. When the owner
/// refuses a connection, the request path removes it at once and retries once on the new owner.
/// Nothing is persisted: a restarted router derives the same ring from its configuration.

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "common/consistentHash.h"

namespace outpost
{
namespace router
{

/// @brief One edge the router can forward to.
struct EdgeNode
{
    std::string id;    ///< Node id, also its identity on the hash ring.
    std::string host;  ///< Hostname or address.
    int port = 0;      ///< HTTP port.
};

/// @brief Configuration for a router instance.
struct RouterConfig
{
    std::string bindAddress = "0.0.0.0";                  ///< Listen address.
    int port = 8080;                                      ///< Listen port; 0 picks a free port.
    std::vector<EdgeNode> edges;                          ///< Edges to route to (at least one).
    std::size_t virtualNodes = 150;                       ///< Ring points per edge.
    std::chrono::milliseconds healthCheckInterval{1000};  ///< Time between edge health checks.
    std::chrono::milliseconds edgeTimeout{2000};          ///< Connect/read timeout to an edge.
    int unhealthyThreshold = 2;                           ///< Failed checks that remove an edge.
};

/// @brief Consistent-hash router.
///
/// Lifecycle: bind(), then serve() (blocking); stop() from any thread.
class RouterServer
{
public:
    /// @throws std::invalid_argument when no edge is configured, an id repeats, or
    ///         unhealthyThreshold < 1.
    explicit RouterServer(const RouterConfig& config);
    ~RouterServer();

    RouterServer(const RouterServer&) = delete;
    RouterServer& operator=(const RouterServer&) = delete;

    /// @brief Bind the listening socket. @return The bound port, or -1 on failure.
    int bind();

    /// @brief Start the health thread and serve requests until stop(). Requires bind().
    bool serve();

    /// @brief Stop serving and join the health thread. Safe to call more than once.
    void stop();

private:
    /// @brief Health and routing counters of one configured edge.
    struct EdgeStatus
    {
        EdgeNode node;
        std::atomic<int> consecutiveFailures{0};
        std::atomic<uint64_t> routed{0};
    };

    /// @brief Health thread body: checks every edge and updates ring membership.
    void healthLoop();

    /// @brief Forward a /key request to the key's owner, retrying once on a refused connection.
    void forward(const httplib::Request& req, httplib::Response& res);

    const RouterConfig config_;
    httplib::Server server_;
    common::ConsistentHashRing ring_;
    std::map<std::string, std::unique_ptr<EdgeStatus>> edges_;  ///< Fixed after construction.
    std::atomic<uint64_t> requests_{0};
    std::atomic<uint64_t> failovers_{0};
    std::atomic<uint64_t> purges_{0};

    std::thread healthThread_;
    std::mutex joinMutex_;
    std::mutex stopMutex_;
    std::condition_variable stopSignal_;
    bool stopping_ = false;
};

}  // namespace router
}  // namespace outpost
