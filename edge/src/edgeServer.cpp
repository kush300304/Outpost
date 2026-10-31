/// @file edgeServer.cpp
/// @brief Edge server implementation — cache-aside, degraded mode, health checks.

#include "edge/edgeServer.h"

#include <chrono>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;
using namespace std::chrono;

namespace outpost {
namespace edge {

EdgeServer::EdgeServer(const EdgeConfig& config)
    : config_(config),
      originClient_(config.writeApiHost, config.writeApiPort),
      cache_(config.cacheCapacity) {
    // Initialize edge-local WAL for degraded-mode write queueing
    if (!config_.walPath.empty()) {
        writeQueue_ = std::make_unique<common::WriteAheadLog>(config_.walPath);
    } else {
        // Default path based on node ID
        std::string defaultPath = "/data/" + config_.nodeId + "_write_queue.wal";
        writeQueue_ = std::make_unique<common::WriteAheadLog>(defaultPath);
    }

    registerRoutes();
    spdlog::info("[{}] EdgeServer initialized: cache={} entries, write-api={}:{}", config_.nodeId,
                 config_.cacheCapacity, config_.writeApiHost, config_.writeApiPort);
}

EdgeServer::~EdgeServer() {
    stop();
}

void EdgeServer::run() {
    running_.store(true);

    // Start background health-check thread
    healthCheckThread_ = std::thread(&EdgeServer::healthCheckLoop, this);

    spdlog::info("[{}] Starting edge server on port {}", config_.nodeId, config_.port);
    if (!server_.listen("0.0.0.0", config_.port)) {
        spdlog::error("[{}] Failed to bind to port {}", config_.nodeId, config_.port);
    }
}

void EdgeServer::stop() {
    running_.store(false);
    server_.stop();
    if (healthCheckThread_.joinable()) {
        healthCheckThread_.join();
    }
}

// ── Helper: get current time in ms since epoch ──────────────────────────
static int64_t nowMs() {
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void EdgeServer::registerRoutes() {
    // ── Health check ────────────────────────────────────────────────────
    server_.Get("/health", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        std::string stateStr;
        auto state = breakerState_.load();
        switch (state) {
            case BreakerState::CLOSED: stateStr = "CLOSED"; break;
            case BreakerState::OPEN: stateStr = "OPEN"; break;
            case BreakerState::HALF_OPEN: stateStr = "HALF_OPEN"; break;
        }
        json body = {{"status", "healthy"},
                     {"service", "edge"},
                     {"nodeId", config_.nodeId},
                     {"breakerState", stateStr}};
        res.set_content(body.dump(), "application/json");
    });

    // ── GET /key/:id — cache-aside read ─────────────────────────────────
    server_.Get(R"(/key/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        totalRequests_.fetch_add(1, std::memory_order_relaxed);
        totalReads_.fetch_add(1, std::memory_order_relaxed);

        // 1. Check cache first
        auto cached = cache_.get(key);
        if (cached.has_value()) {
            json body = {{"key", key}, {"value", cached.value()}};
            res.set_header("X-Cache", "HIT");
            res.set_header("X-Node", config_.nodeId);
            res.set_content(body.dump(), "application/json");
            return;
        }

        // 2. Cache miss — check breaker state
        auto state = breakerState_.load();
        if (state == BreakerState::OPEN) {
            // Breaker is open — try to serve stale (but we already checked cache above,
            // so there's truly nothing cached for this key). Return 503.
            staleServes_.fetch_add(1, std::memory_order_relaxed);
            json body = {{"error", "service_degraded"},
                         {"message", "Origin unavailable and key not in cache"},
                         {"key", key}};
            res.status = 503;
            res.set_header("X-Cache", "MISS_DEGRADED");
            res.set_header("X-Node", config_.nodeId);
            res.set_content(body.dump(), "application/json");
            return;
        }

        // 3. Fetch from origin
        auto value = originClient_.get(key);
        if (!value.has_value()) {
            // Origin returned 404 or failed
            json body = {{"error", "not_found"}, {"key", key}};
            res.status = 404;
            res.set_header("X-Cache", "MISS");
            res.set_header("X-Node", config_.nodeId);
            res.set_content(body.dump(), "application/json");
            return;
        }

        // 4. Populate cache and return
        cache_.put(key, value.value());
        json body = {{"key", key}, {"value", value.value()}};
        res.set_header("X-Cache", "MISS");
        res.set_header("X-Node", config_.nodeId);
        res.set_content(body.dump(), "application/json");
        spdlog::debug("[{}] Cache populated: key={}", config_.nodeId, key);
    });

    // ── PUT /key/:id — write (or queue if breaker open) ─────────────────
    server_.Put(R"(/key/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        totalRequests_.fetch_add(1, std::memory_order_relaxed);
        totalWrites_.fetch_add(1, std::memory_order_relaxed);

        // Parse value from body
        std::string value;
        if (!req.body.empty()) {
            try {
                auto parsed = json::parse(req.body);
                if (parsed.contains("value") && parsed["value"].is_string()) {
                    value = parsed["value"].get<std::string>();
                } else {
                    json errBody = {{"error", "bad_request"},
                                    {"message", "JSON body must contain a string \"value\" field"}};
                    res.status = 400;
                    res.set_content(errBody.dump(), "application/json");
                    return;
                }
            } catch (const json::parse_error&) {
                value = req.body;
            }
        } else {
            json errBody = {{"error", "bad_request"}, {"message", "Request body is required"}};
            res.status = 400;
            res.set_content(errBody.dump(), "application/json");
            return;
        }

        auto state = breakerState_.load();
        if (state == BreakerState::OPEN) {
            // Breaker open — queue write locally instead of forwarding
            if (writeQueue_) {
                writeQueue_->appendPut(key, value);
                queuedWrites_.fetch_add(1, std::memory_order_relaxed);
            }
            // Update local cache optimistically
            cache_.put(key, value);

            json body = {{"key", key},
                         {"value", value},
                         {"queued", true},
                         {"message", "Write queued locally — origin unavailable"}};
            res.set_header("X-Write-Mode", "QUEUED");
            res.set_header("X-Node", config_.nodeId);
            res.set_content(body.dump(), "application/json");
            spdlog::warn("[{}] Write queued (breaker OPEN): key={}", config_.nodeId, key);
            return;
        }

        // Breaker closed/half-open — forward to origin
        auto originRes = originClient_.put(key, value);
        if (!originRes.success) {
            // Origin write failed — record failure for breaker
            consecutiveFailures_.fetch_add(1, std::memory_order_relaxed);

            // Queue the write locally so it's not lost
            if (writeQueue_) {
                writeQueue_->appendPut(key, value);
                queuedWrites_.fetch_add(1, std::memory_order_relaxed);
            }
            cache_.put(key, value);

            json body = {{"key", key},
                         {"value", value},
                         {"queued", true},
                         {"message", "Origin write failed, queued locally"}};
            res.status = 202;  // Accepted but not persisted to origin yet
            res.set_header("X-Write-Mode", "QUEUED");
            res.set_header("X-Node", config_.nodeId);
            res.set_content(body.dump(), "application/json");
            return;
        }

        // Success — update cache and reset failure counter
        consecutiveFailures_.store(0, std::memory_order_relaxed);
        cache_.put(key, value);

        res.set_header("X-Write-Mode", "DIRECT");
        res.set_header("X-Node", config_.nodeId);
        res.set_content(originRes.body, "application/json");
    });

    // ── GET /debug/stats — observability ────────────────────────────────
    server_.Get("/debug/stats", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        auto cacheStats = cache_.stats();

        std::string stateStr;
        auto state = breakerState_.load();
        switch (state) {
            case BreakerState::CLOSED: stateStr = "CLOSED"; break;
            case BreakerState::OPEN: stateStr = "OPEN"; break;
            case BreakerState::HALF_OPEN: stateStr = "HALF_OPEN"; break;
        }

        json body = {
            {"service", "edge"},
            {"nodeId", config_.nodeId},
            {"cache",
             {{"size", cache_.size()},
              {"capacity", cache_.capacity()},
              {"hits", cacheStats.hits},
              {"misses", cacheStats.misses},
              {"evictions", cacheStats.evictions},
              {"hitRate", cacheStats.hitRate()}}},
            {"circuitBreaker",
             {{"state", stateStr}, {"consecutiveFailures", consecutiveFailures_.load()}}},
            {"requests",
             {{"total", totalRequests_.load()},
              {"reads", totalReads_.load()},
              {"writes", totalWrites_.load()},
              {"staleServes", staleServes_.load()},
              {"queuedWrites", queuedWrites_.load()},
              {"reconciledWrites", reconciledWrites_.load()},
              {"rateLimited", rateLimited_.load()}}},
        };
        res.set_content(body.dump(2), "application/json");
    });
}

void EdgeServer::healthCheckLoop() {
    spdlog::info("[{}] Health-check thread started (interval={}s)", config_.nodeId,
                 config_.healthCheckIntervalSec);

    while (running_.load()) {
        // Sleep for the configured interval (in short increments so we can exit quickly)
        for (int i = 0; i < config_.healthCheckIntervalSec * 10 && running_.load(); ++i) {
            std::this_thread::sleep_for(milliseconds(100));
        }

        if (!running_.load()) break;

        bool healthy = originClient_.healthCheck();

        auto currentState = breakerState_.load();

        if (healthy) {
            if (currentState == BreakerState::OPEN || currentState == BreakerState::HALF_OPEN) {
                spdlog::info("[{}] Origin healthy — closing circuit breaker", config_.nodeId);
                breakerState_.store(BreakerState::CLOSED);
                consecutiveFailures_.store(0);

                // Reconcile queued writes
                reconcileQueuedWrites();
            }
            consecutiveFailures_.store(0);
        } else {
            int failures = consecutiveFailures_.fetch_add(1, std::memory_order_relaxed) + 1;
            spdlog::warn("[{}] Health check failed (consecutive: {})", config_.nodeId, failures);

            if (currentState == BreakerState::CLOSED &&
                failures >= config_.breakerFailureThreshold) {
                spdlog::error("[{}] Circuit breaker OPENED after {} consecutive failures",
                              config_.nodeId, failures);
                breakerState_.store(BreakerState::OPEN);
                breakerOpenedAtMs_.store(nowMs());
            } else if (currentState == BreakerState::OPEN) {
                // Check if enough time has passed to try half-open
                int64_t elapsed = nowMs() - breakerOpenedAtMs_.load();
                if (elapsed >= config_.breakerResetTimeoutSec * 1000) {
                    spdlog::info("[{}] Breaker timeout reached — transitioning to HALF_OPEN",
                                 config_.nodeId);
                    breakerState_.store(BreakerState::HALF_OPEN);
                }
            }
        }
    }

    spdlog::info("[{}] Health-check thread stopped", config_.nodeId);
}

void EdgeServer::reconcileQueuedWrites() {
    if (!writeQueue_) return;

    auto entries = writeQueue_->replay();
    if (entries.empty()) {
        spdlog::info("[{}] No queued writes to reconcile", config_.nodeId);
        return;
    }

    spdlog::info("[{}] Reconciling {} queued writes...", config_.nodeId, entries.size());

    std::size_t succeeded = 0;
    std::size_t failed = 0;

    for (const auto& entry : entries) {
        if (entry.operation == common::WalOperation::PUT) {
            auto result = originClient_.put(entry.key, entry.value);
            if (result.success) {
                ++succeeded;
            } else {
                ++failed;
                spdlog::error("[{}] Reconciliation failed for key={}: {}", config_.nodeId,
                              entry.key, result.errorMessage);
                // Don't truncate WAL if reconciliation fails — we'll retry next time
                if (failed > 3) {
                    spdlog::error("[{}] Too many reconciliation failures, aborting", config_.nodeId);
                    return;
                }
            }
        }
    }

    reconciledWrites_.fetch_add(succeeded, std::memory_order_relaxed);

    if (failed == 0) {
        // All writes reconciled — truncate the queue
        writeQueue_->truncate();
        spdlog::info("[{}] Reconciliation complete: {} writes applied, WAL truncated",
                     config_.nodeId, succeeded);
    } else {
        spdlog::warn("[{}] Reconciliation partial: {} succeeded, {} failed (WAL kept)",
                     config_.nodeId, succeeded, failed);
    }
}

}  // namespace edge
}  // namespace outpost
