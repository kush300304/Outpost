/// @file edgeServer.cpp
/// @brief Edge server implementation: cache-aside, degraded mode, health checks.

#include "edge/edgeServer.h"

#include <algorithm>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;
using namespace std::chrono;

namespace outpost {
namespace edge {

EdgeServer::EdgeServer(const EdgeConfig& config)
    : config_(config),
      originClient_(config.writeApiHost, config.writeApiPort, config.originTimeout),
      cache_(config.cacheCapacity),
      writeQueue_(config.walPath.empty() ? "/data/" + config.nodeId + "_write_queue.wal"
                                         : config.walPath,
                  config.walFsync),
      breaker_(common::CircuitBreakerConfig{config.breakerFailureThreshold,
                                            config.breakerResetTimeout}) {
    breaker_.onTransition([this](common::BreakerState from, common::BreakerState to) {
        spdlog::warn("[{}] Circuit breaker {} -> {}", config_.nodeId,
                     common::breakerStateToString(from), common::breakerStateToString(to));
    });

    server_.Get("/health", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"status", "healthy"},
                     {"service", "edge"},
                     {"nodeId", config_.nodeId},
                     {"breakerState", common::breakerStateToString(breaker_.state())}};
        res.set_content(body.dump(), "application/json");
    });

    server_.Get(R"(/key/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        totalRequests_.fetch_add(1, std::memory_order_relaxed);
        totalReads_.fetch_add(1, std::memory_order_relaxed);

        json body;
        std::string cacheHeader = "MISS";
        bool closed = breaker_.state() == common::BreakerState::CLOSED;
        auto cached = cache_.get(key);
        if (cached.has_value()) {
            cacheHeader = closed ? "HIT" : "STALE";
            staleServes_.fetch_add(closed ? 0 : 1, std::memory_order_relaxed);
            body = {{"key", key}, {"value", cached.value()}};
        } else if (!closed) {
            res.status = 503;
            body = {{"error", "origin_unavailable"},
                    {"message", "origin unavailable and key not cached at this edge"},
                    {"key", key}};
        } else {
            OriginResponse origin = originClient_.get(key);
            json parsed = json::parse(origin.body, nullptr, false);
            bool found = origin.httpStatus == 200 && parsed.is_object() &&
                         parsed.contains("value") && parsed["value"].is_string();
            if (found) {
                breaker_.recordSuccess();
                cache_.put(key, parsed["value"].get<std::string>());
                body = {{"key", key}, {"value", parsed["value"]}};
            } else if (origin.httpStatus == 404) {
                breaker_.recordSuccess();
                res.status = 404;
                body = {{"error", "not_found"}, {"key", key}};
            } else {
                breaker_.recordFailure();
                res.status = 503;
                body = {{"error", "origin_unavailable"},
                        {"message", origin.errorMessage.empty()
                                        ? "origin returned HTTP " +
                                              std::to_string(origin.httpStatus)
                                        : origin.errorMessage},
                        {"key", key}};
            }
        }
        res.set_header("X-Cache", cacheHeader);
        res.set_header("X-Node", config_.nodeId);
        res.set_content(body.dump(), "application/json");
    });

    server_.Put(R"(/key/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        totalRequests_.fetch_add(1, std::memory_order_relaxed);
        totalWrites_.fetch_add(1, std::memory_order_relaxed);

        std::string value = req.body;
        bool valid = !req.body.empty();
        json parsed = json::parse(req.body, nullptr, false);
        if (!parsed.is_discarded()) {
            valid = parsed.is_object() && parsed.contains("value") && parsed["value"].is_string();
            value = valid ? parsed["value"].get<std::string>() : std::string();
        }

        std::string content;
        if (!valid) {
            res.status = 400;
            content = json({{"error", "bad_request"},
                            {"message", "body must be raw text or JSON {\"value\":string}"}})
                          .dump();
        } else {
            uint64_t now = static_cast<uint64_t>(
                duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
            uint64_t previous = lastVersion_.load();
            uint64_t version = std::max(now, previous + 1);
            while (!lastVersion_.compare_exchange_weak(previous, version)) {
                version = std::max(now, previous + 1);
            }
            common::WalEntry queued;
            queued.operation = common::WalOperation::PUT;
            queued.key = key;
            queued.value = value;
            queued.version = version;
            queued.writerId = config_.nodeId;

            bool forwarded = false;
            bool rejected = false;
            if (breaker_.state() == common::BreakerState::CLOSED) {
                OriginResponse originRes = originClient_.put(key, value, version, config_.nodeId);
                forwarded = originRes.success;
                rejected = originRes.httpStatus >= 400 && originRes.httpStatus < 500;
                if (forwarded || rejected) {
                    breaker_.recordSuccess();
                    res.status = originRes.httpStatus;
                    content = originRes.body;
                } else {
                    breaker_.recordFailure();
                }
            }
            if (!forwarded && !rejected) {
                writeQueue_.append(queued);
                queuedWrites_.fetch_add(1, std::memory_order_relaxed);
                res.status = 202;
                content = json({{"key", key},
                                {"value", value},
                                {"queued", true},
                                {"message", "origin unavailable, write queued at the edge"}})
                              .dump();
            }
            if (!rejected) {
                cache_.put(key, value);
                res.set_header("X-Write-Mode", forwarded ? "DIRECT" : "QUEUED");
            }
        }
        res.set_header("X-Node", config_.nodeId);
        res.set_content(content, "application/json");
    });

    server_.Get("/debug/stats", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        auto cacheStats = cache_.stats();
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
             {{"state", common::breakerStateToString(breaker_.state())},
              {"consecutiveFailures", breaker_.consecutiveFailures()},
              {"failureThreshold", config_.breakerFailureThreshold},
              {"resetTimeoutMs", config_.breakerResetTimeout.count()}}},
            {"writeQueue", {{"pending", writeQueue_.size()}}},
            {"requests",
             {{"total", totalRequests_.load()},
              {"reads", totalReads_.load()},
              {"writes", totalWrites_.load()},
              {"staleServes", staleServes_.load()},
              {"queuedWrites", queuedWrites_.load()},
              {"reconciledWrites", reconciledWrites_.load()},
              {"rejectedWrites", rejectedWrites_.load()},
              {"rateLimited", rateLimited_.load()}}},
        };
        res.set_content(body.dump(2), "application/json");
    });

    spdlog::info("[{}] EdgeServer initialized: cache={} entries, write-api={}:{}, queued={}",
                 config_.nodeId, config_.cacheCapacity, config_.writeApiHost,
                 config_.writeApiPort, writeQueue_.size());
}

EdgeServer::~EdgeServer() {
    stop();
}

int EdgeServer::bind() {
    int port = config_.port == 0 ? server_.bind_to_any_port(config_.bindAddress)
                                 : (server_.bind_to_port(config_.bindAddress, config_.port)
                                        ? config_.port
                                        : -1);
    if (port < 0) {
        spdlog::error("[{}] Failed to bind {}:{}", config_.nodeId, config_.bindAddress,
                      config_.port);
    }
    return port;
}

bool EdgeServer::serve() {
    {
        std::lock_guard<std::mutex> lock(stopMutex_);
        stopping_ = false;
    }
    maintenanceThread_ = std::thread(&EdgeServer::maintenanceLoop, this);
    spdlog::info("[{}] Serving", config_.nodeId);
    bool served = server_.listen_after_bind();
    stop();
    return served;
}

void EdgeServer::stop() {
    {
        std::lock_guard<std::mutex> lock(stopMutex_);
        stopping_ = true;
    }
    stopSignal_.notify_all();
    server_.stop();
    std::lock_guard<std::mutex> joinLock(joinMutex_);
    if (maintenanceThread_.joinable()) {
        maintenanceThread_.join();
    }
}

void EdgeServer::maintenanceLoop() {
    spdlog::info("[{}] Maintenance thread started (interval={}ms)", config_.nodeId,
                 config_.healthCheckInterval.count());
    std::unique_lock<std::mutex> lock(stopMutex_);
    while (!stopSignal_.wait_for(lock, config_.healthCheckInterval, [this] { return stopping_; })) {
        lock.unlock();
        if (breaker_.isAllowed()) {
            bool healthy = originClient_.healthCheck();
            if (healthy) {
                breaker_.recordSuccess();
            } else {
                breaker_.recordFailure();
                spdlog::warn("[{}] Health check failed (consecutive: {})", config_.nodeId,
                             breaker_.consecutiveFailures());
            }
        }
        if (breaker_.state() == common::BreakerState::CLOSED && writeQueue_.size() > 0) {
            // Drain: send queued writes in order with their original versions and discard
            // exactly the prefix the origin resolved; writes queued meanwhile stay queued.
            std::vector<common::WalEntry> entries = writeQueue_.replay();
            spdlog::info("[{}] Reconciling {} queued writes", config_.nodeId, entries.size());
            uint64_t resolvedThrough = 0;
            std::size_t delivered = 0;
            std::size_t rejected = 0;
            bool originFailed = false;
            std::size_t index = 0;
            while (!originFailed && index < entries.size()) {
                const common::WalEntry& entry = entries[index];
                OriginResponse result =
                    originClient_.put(entry.key, entry.value, entry.version, entry.writerId);
                if (result.success) {
                    resolvedThrough = entry.sequenceNumber;
                    ++delivered;
                } else if (result.httpStatus >= 400 && result.httpStatus < 500) {
                    resolvedThrough = entry.sequenceNumber;
                    ++rejected;
                    spdlog::error("[{}] Queued write for key={} rejected (HTTP {}): dropped",
                                  config_.nodeId, entry.key, result.httpStatus);
                } else {
                    originFailed = true;
                    breaker_.recordFailure();
                }
                ++index;
            }
            std::size_t remaining = resolvedThrough > 0
                                        ? writeQueue_.discardThrough(resolvedThrough)
                                        : writeQueue_.size();
            reconciledWrites_.fetch_add(delivered, std::memory_order_relaxed);
            rejectedWrites_.fetch_add(rejected, std::memory_order_relaxed);
            spdlog::info("[{}] Reconciliation finished: {} delivered, {} rejected, {} still queued",
                         config_.nodeId, delivered, rejected, remaining);
        }
        lock.lock();
    }
    spdlog::info("[{}] Maintenance thread stopped", config_.nodeId);
}

}  // namespace edge
}  // namespace outpost
