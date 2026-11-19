/// @file edgeServer.cpp
/// @brief Edge server implementation: cache-aside, degraded mode, health checks.

#include "edge/edgeServer.h"

#include <algorithm>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;
using namespace std::chrono;

namespace outpost {
namespace edge {

namespace {

const char* breakerName(int state) {
    const char* names[] = {"CLOSED", "OPEN", "HALF_OPEN"};
    return names[state];
}

}  // namespace

EdgeServer::EdgeServer(const EdgeConfig& config)
    : config_(config),
      originClient_(config.writeApiHost, config.writeApiPort, config.originTimeout),
      cache_(config.cacheCapacity),
      writeQueue_(config.walPath.empty() ? "/data/" + config.nodeId + "_write_queue.wal"
                                         : config.walPath,
                  config.walFsync) {
    registerRoutes();
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
    if (maintenanceThread_.joinable() && maintenanceThread_.get_id() != std::this_thread::get_id()) {
        maintenanceThread_.join();
    }
}

void EdgeServer::registerRoutes() {
    server_.Get("/health", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"status", "healthy"},
                     {"service", "edge"},
                     {"nodeId", config_.nodeId},
                     {"breakerState", breakerName(static_cast<int>(breakerState_.load()))}};
        res.set_content(body.dump(), "application/json");
    });

    server_.Get(R"(/key/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        totalRequests_.fetch_add(1, std::memory_order_relaxed);
        totalReads_.fetch_add(1, std::memory_order_relaxed);

        json body;
        std::string cacheHeader = "MISS";
        auto cached = cache_.get(key);
        if (cached.has_value()) {
            cacheHeader = "HIT";
            body = {{"key", key}, {"value", cached.value()}};
        } else if (breakerState_.load() == BreakerState::OPEN) {
            staleServes_.fetch_add(1, std::memory_order_relaxed);
            res.status = 503;
            cacheHeader = "MISS_DEGRADED";
            body = {{"error", "service_degraded"},
                    {"message", "Origin unavailable and key not in cache"},
                    {"key", key}};
        } else {
            auto value = originClient_.get(key);
            if (value.has_value()) {
                cache_.put(key, value.value());
                body = {{"key", key}, {"value", value.value()}};
            } else {
                res.status = 404;
                body = {{"error", "not_found"}, {"key", key}};
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
            if (breakerState_.load() != BreakerState::OPEN) {
                OriginResponse originRes = originClient_.put(key, value, version, config_.nodeId);
                forwarded = originRes.success;
                if (forwarded) {
                    consecutiveFailures_.store(0, std::memory_order_relaxed);
                    content = originRes.body;
                } else {
                    consecutiveFailures_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            if (!forwarded) {
                writeQueue_.append(queued);
                queuedWrites_.fetch_add(1, std::memory_order_relaxed);
                res.status = 202;
                content = json({{"key", key},
                                {"value", value},
                                {"queued", true},
                                {"message", "origin unavailable, write queued at the edge"}})
                              .dump();
            }
            cache_.put(key, value);
            res.set_header("X-Write-Mode", forwarded ? "DIRECT" : "QUEUED");
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
             {{"state", breakerName(static_cast<int>(breakerState_.load()))},
              {"consecutiveFailures", consecutiveFailures_.load()}}},
            {"writeQueue", {{"pending", writeQueue_.size()}}},
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

void EdgeServer::maintenanceLoop() {
    spdlog::info("[{}] Maintenance thread started (interval={}ms)", config_.nodeId,
                 config_.healthCheckInterval.count());
    std::unique_lock<std::mutex> lock(stopMutex_);
    while (!stopSignal_.wait_for(lock, config_.healthCheckInterval, [this] { return stopping_; })) {
        lock.unlock();
        bool healthy = originClient_.healthCheck();
        BreakerState state = breakerState_.load();
        int64_t nowMs = duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
        if (healthy) {
            consecutiveFailures_.store(0);
            if (state != BreakerState::CLOSED) {
                spdlog::info("[{}] Origin healthy — closing circuit breaker", config_.nodeId);
                breakerState_.store(BreakerState::CLOSED);
                reconcileQueuedWrites();
            }
        } else {
            int failures = consecutiveFailures_.fetch_add(1) + 1;
            spdlog::warn("[{}] Health check failed (consecutive: {})", config_.nodeId, failures);
            if (state == BreakerState::CLOSED && failures >= config_.breakerFailureThreshold) {
                spdlog::error("[{}] Circuit breaker OPENED after {} consecutive failures",
                              config_.nodeId, failures);
                breakerState_.store(BreakerState::OPEN);
                breakerOpenedAtMs_.store(nowMs);
            } else if (state == BreakerState::OPEN &&
                       nowMs - breakerOpenedAtMs_.load() >= config_.breakerResetTimeout.count()) {
                breakerState_.store(BreakerState::HALF_OPEN);
            }
        }
        lock.lock();
    }
    spdlog::info("[{}] Maintenance thread stopped", config_.nodeId);
}

void EdgeServer::reconcileQueuedWrites() {
    auto entries = writeQueue_.replay();
    spdlog::info("[{}] Reconciling {} queued writes", config_.nodeId, entries.size());
    std::size_t succeeded = 0;
    std::size_t failed = 0;
    for (const auto& entry : entries) {
        auto result = originClient_.put(entry.key, entry.value, entry.version, entry.writerId);
        succeeded += result.success ? 1 : 0;
        failed += result.success ? 0 : 1;
    }
    reconciledWrites_.fetch_add(succeeded, std::memory_order_relaxed);
    if (failed == 0) {
        writeQueue_.truncate();
    }
    spdlog::info("[{}] Reconciliation finished: {} applied, {} failed", config_.nodeId, succeeded,
                 failed);
}

}  // namespace edge
}  // namespace outpost
