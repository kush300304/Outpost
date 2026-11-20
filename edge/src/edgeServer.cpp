/// @file edgeServer.cpp
/// @brief Edge server implementation: cache-aside, degraded mode, health checks.

#include "edge/edgeServer.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "common/requestLimits.h"

using json = nlohmann::json;
using namespace std::chrono;

namespace outpost
{
namespace edge
{

namespace
{

/// The requesting client: the first X-Forwarded-For hop (set by the router on a trusted network),
/// or the socket peer address when the header is absent.
std::string clientIdOf(const httplib::Request& req)
{
    std::string forwarded = req.get_header_value("X-Forwarded-For");
    std::string first = forwarded.substr(0, forwarded.find(','));
    first.erase(0, first.find_first_not_of(' '));
    first.erase(first.find_last_not_of(' ') + 1);
    return first.empty() ? req.remote_addr : first;
}

/// Milliseconds since the Unix epoch, for event timestamps.
int64_t epochMs()
{
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

const char* const kRateLimitedBody =
    R"({"error":"rate_limited","message":"too many requests from this client"})";

}  // namespace

EdgeServer::EdgeServer(const EdgeConfig& config)
    : config_(config),
      originClient_(config.writeApiHost, config.writeApiPort, config.originTimeout),
      cache_(config.cacheCapacity, config.cacheTtl),
      writeQueue_(
          config.walPath.empty() ? "/data/" + config.nodeId + "_write_queue.wal" : config.walPath,
          config.walFsync),
      breaker_(
          common::CircuitBreakerConfig{config.breakerFailureThreshold, config.breakerResetTimeout})
{
    if (config_.maxTrackedClients == 0 || config_.rateLimitBurst == 0 ||
        config_.rateLimitPerSec < 0.0)
    {
        throw std::invalid_argument(
            "EdgeConfig: maxTrackedClients and rateLimitBurst must be > 0, rateLimitPerSec >= 0");
    }
    breaker_.onTransition([this](common::BreakerState from, common::BreakerState to) {
        std::string change = std::string(common::breakerStateToString(from)) + " -> " +
                             common::breakerStateToString(to);
        spdlog::warn("[{}] Circuit breaker {}", config_.nodeId, change);
        {
            std::lock_guard<std::mutex> lock(eventsMutex_);
            transitions_.push_back(Transition{epochMs(), common::breakerStateToString(from),
                                              common::breakerStateToString(to)});
            if (transitions_.size() > kMaxTransitions)
            {
                transitions_.pop_front();
            }
        }
        recordEvent("breaker", change);
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
        std::string clientId = clientIdOf(req);
        bool admitted = bucketFor(clientId)->tryConsume();
        bool closed = breaker_.state() == common::BreakerState::CLOSED;
        auto cached = admitted ? cache_.lookup(key) : std::nullopt;
        if (!admitted)
        {
            recordRateLimit(clientId);
            res.status = 429;
            res.set_header("Retry-After", "1");
            cacheHeader.clear();
            body = json::parse(kRateLimitedBody);
        }
        else if (cached && (cached->fresh || !closed))
        {
            cacheHeader = closed ? "HIT" : "STALE";
            staleServes_.fetch_add(closed ? 0 : 1, std::memory_order_relaxed);
            body = {{"key", key}, {"value", cached->value}};
        }
        else if (!closed)
        {
            res.status = 503;
            body = {{"error", "origin_unavailable"},
                    {"message", "origin unavailable and key not cached at this edge"},
                    {"key", key}};
        }
        else
        {
            OriginResponse origin = originClient_.get(key);
            json parsed = json::parse(origin.body, nullptr, false);
            bool found = origin.httpStatus == 200 && parsed.is_object() &&
                         parsed.contains("value") && parsed["value"].is_string();
            if (found)
            {
                breaker_.recordSuccess();
                cache_.put(key, parsed["value"].get<std::string>(),
                           parsed.value("version", uint64_t{0}));
                body = {{"key", key}, {"value", parsed["value"]}};
            }
            else if (origin.httpStatus == 404)
            {
                breaker_.recordSuccess();
                cache_.remove(key);
                res.status = 404;
                body = {{"error", "not_found"}, {"key", key}};
            }
            else if (cached)
            {
                breaker_.recordFailure();
                cacheHeader = "STALE";
                staleServes_.fetch_add(1, std::memory_order_relaxed);
                body = {{"key", key}, {"value", cached->value}};
            }
            else
            {
                breaker_.recordFailure();
                res.status = 503;
                body = {
                    {"error", "origin_unavailable"},
                    {"message", origin.errorMessage.empty()
                                    ? "origin returned HTTP " + std::to_string(origin.httpStatus)
                                    : origin.errorMessage},
                    {"key", key}};
            }
        }
        if (!cacheHeader.empty())
        {
            res.set_header("X-Cache", cacheHeader);
        }
        res.set_header("X-Node", config_.nodeId);
        res.set_content(body.dump(), "application/json");
    });

    server_.Put(R"(/key/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        totalRequests_.fetch_add(1, std::memory_order_relaxed);
        totalWrites_.fetch_add(1, std::memory_order_relaxed);

        std::string value = req.body;
        std::string invalid = req.body.empty() ? "request body is required" : "";
        json parsed = json::parse(req.body, nullptr, false);
        if (!parsed.is_discarded())
        {
            bool shaped =
                parsed.is_object() && parsed.contains("value") && parsed["value"].is_string();
            value = shaped ? parsed["value"].get<std::string>() : std::string();
            invalid = shaped ? "" : "body must be raw text or JSON {\"value\":string}";
        }
        if (key.size() > common::kMaxKeyBytes)
        {
            invalid = "key exceeds 256 bytes";
        }
        else if (value.size() > common::kMaxValueBytes)
        {
            invalid = "value exceeds 1 MiB";
        }

        std::string content;
        std::string clientId = clientIdOf(req);
        bool admitted = bucketFor(clientId)->tryConsume();
        if (!admitted)
        {
            recordRateLimit(clientId);
            res.status = 429;
            res.set_header("Retry-After", "1");
            content = kRateLimitedBody;
        }
        else if (!invalid.empty())
        {
            res.status = 400;
            content = json({{"error", "bad_request"}, {"message", invalid}}).dump();
        }
        else
        {
            uint64_t now = static_cast<uint64_t>(
                duration_cast<microseconds>(system_clock::now().time_since_epoch()).count());
            uint64_t previous = lastVersion_.load();
            uint64_t version = std::max(now, previous + 1);
            while (!lastVersion_.compare_exchange_weak(previous, version))
            {
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
            if (breaker_.state() == common::BreakerState::CLOSED)
            {
                OriginResponse originRes = originClient_.put(key, value, version, config_.nodeId);
                forwarded = originRes.success;
                rejected = originRes.httpStatus >= 400 && originRes.httpStatus < 500;
                if (forwarded || rejected)
                {
                    breaker_.recordSuccess();
                    res.status = originRes.httpStatus;
                    content = originRes.body;
                }
                else
                {
                    breaker_.recordFailure();
                }
            }
            if (!forwarded && !rejected)
            {
                writeQueue_.append(queued);
                queuedWrites_.fetch_add(1, std::memory_order_relaxed);
                res.status = 202;
                content = json({{"key", key},
                                {"value", value},
                                {"queued", true},
                                {"message", "origin unavailable, write queued at the edge"}})
                              .dump();
            }
            if (!rejected)
            {
                cache_.put(key, value, version);
                res.set_header("X-Write-Mode", forwarded ? "DIRECT" : "QUEUED");
            }
        }
        res.set_header("X-Node", config_.nodeId);
        res.set_content(content, "application/json");
    });

    server_.Get("/debug/stats", [this](const httplib::Request& req, httplib::Response& res) {
        auto cacheStats = cache_.stats();
        double callerTokens = bucketFor(clientIdOf(req))->tokensAvailable();
        std::size_t trackedClients = 0;
        {
            std::lock_guard<std::mutex> lock(clientsMutex_);
            trackedClients = clients_.size();
        }
        json transitions = json::array();
        json events = json::array();
        {
            std::lock_guard<std::mutex> lock(eventsMutex_);
            for (const Transition& transition : transitions_)
            {
                transitions.push_back(
                    {{"atMs", transition.atMs}, {"from", transition.from}, {"to", transition.to}});
            }
            for (const Event& event : events_)
            {
                events.push_back(
                    {{"atMs", event.atMs}, {"type", event.type}, {"message", event.message}});
            }
        }
        json body = {
            {"service", "edge"},
            {"nodeId", config_.nodeId},
            {"uptimeMs", duration_cast<milliseconds>(steady_clock::now() - startedAt_).count()},
            {"cache",
             {{"size", cache_.size()},
              {"capacity", cache_.capacity()},
              {"ttlMs", cache_.ttl().count()},
              {"hits", cacheStats.hits},
              {"misses", cacheStats.misses},
              {"staleServes", staleServes_.load()},
              {"evictions", cacheStats.evictions},
              {"hitRate", cacheStats.hitRate()}}},
            {"circuitBreaker",
             {{"state", common::breakerStateToString(breaker_.state())},
              {"consecutiveFailures", breaker_.consecutiveFailures()},
              {"failureThreshold", config_.breakerFailureThreshold},
              {"resetTimeoutMs", config_.breakerResetTimeout.count()},
              {"recentTransitions", transitions}}},
            {"rateLimiter",
             {{"capacity", config_.rateLimitBurst},
              {"refillPerSec", config_.rateLimitPerSec},
              {"trackedClients", trackedClients},
              {"callerTokensRemaining", callerTokens},
              {"rejected", rateLimited_.load()}}},
            {"writeQueue",
             {{"pending", writeQueue_.size()},
              {"queued", queuedWrites_.load()},
              {"reconciled", reconciledWrites_.load()},
              {"rejected", rejectedWrites_.load()}}},
            {"requests",
             {{"total", totalRequests_.load()},
              {"reads", totalReads_.load()},
              {"writes", totalWrites_.load()}}},
            {"events", events},
        };
        res.set_content(body.dump(2), "application/json");
    });

    spdlog::info("[{}] EdgeServer initialized: cache={} entries, write-api={}:{}, queued={}",
                 config_.nodeId, config_.cacheCapacity, config_.writeApiHost, config_.writeApiPort,
                 writeQueue_.size());
}

EdgeServer::~EdgeServer()
{
    stop();
}

int EdgeServer::bind()
{
    int port = config_.port == 0
                   ? server_.bind_to_any_port(config_.bindAddress)
                   : (server_.bind_to_port(config_.bindAddress, config_.port) ? config_.port : -1);
    if (port < 0)
    {
        spdlog::error("[{}] Failed to bind {}:{}", config_.nodeId, config_.bindAddress,
                      config_.port);
    }
    return port;
}

bool EdgeServer::serve()
{
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

void EdgeServer::stop()
{
    {
        std::lock_guard<std::mutex> lock(stopMutex_);
        stopping_ = true;
    }
    stopSignal_.notify_all();
    server_.stop();
    std::lock_guard<std::mutex> joinLock(joinMutex_);
    if (maintenanceThread_.joinable())
    {
        maintenanceThread_.join();
    }
}

void EdgeServer::maintenanceLoop()
{
    spdlog::info("[{}] Maintenance thread started (interval={}ms)", config_.nodeId,
                 config_.healthCheckInterval.count());
    std::unique_lock<std::mutex> lock(stopMutex_);
    while (!stopSignal_.wait_for(lock, config_.healthCheckInterval, [this] { return stopping_; }))
    {
        lock.unlock();
        if (breaker_.isAllowed())
        {
            bool healthy = originClient_.healthCheck();
            if (healthy)
            {
                breaker_.recordSuccess();
            }
            else
            {
                breaker_.recordFailure();
                spdlog::warn("[{}] Health check failed (consecutive: {})", config_.nodeId,
                             breaker_.consecutiveFailures());
            }
        }
        if (breaker_.state() == common::BreakerState::CLOSED && writeQueue_.size() > 0)
        {
            // Drain: send queued writes in order with their original versions and discard
            // exactly the prefix the origin resolved; writes queued meanwhile stay queued.
            std::vector<common::WalEntry> entries = writeQueue_.replay();
            spdlog::info("[{}] Reconciling {} queued writes", config_.nodeId, entries.size());
            recordEvent("reconcile",
                        "started: " + std::to_string(entries.size()) + " queued writes");
            uint64_t resolvedThrough = 0;
            std::size_t delivered = 0;
            std::size_t rejected = 0;
            bool originFailed = false;
            std::size_t index = 0;
            while (!originFailed && index < entries.size())
            {
                const common::WalEntry& entry = entries[index];
                OriginResponse result =
                    originClient_.put(entry.key, entry.value, entry.version, entry.writerId);
                if (result.success)
                {
                    resolvedThrough = entry.sequenceNumber;
                    ++delivered;
                }
                else if (result.httpStatus >= 400 && result.httpStatus < 500)
                {
                    resolvedThrough = entry.sequenceNumber;
                    ++rejected;
                    spdlog::error("[{}] Queued write for key={} rejected (HTTP {}): dropped",
                                  config_.nodeId, entry.key, result.httpStatus);
                }
                else
                {
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
            recordEvent("reconcile", "finished: " + std::to_string(delivered) + " delivered, " +
                                         std::to_string(rejected) + " rejected, " +
                                         std::to_string(remaining) + " still queued");
        }
        lock.lock();
    }
    spdlog::info("[{}] Maintenance thread stopped", config_.nodeId);
}

void EdgeServer::recordEvent(const std::string& type, const std::string& message)
{
    std::lock_guard<std::mutex> lock(eventsMutex_);
    events_.push_back(Event{epochMs(), type, message});
    if (events_.size() > kMaxEvents)
    {
        events_.pop_front();
    }
}

void EdgeServer::recordRateLimit(const std::string& clientId)
{
    rateLimited_.fetch_add(1, std::memory_order_relaxed);
    rateLimitedSinceEvent_.fetch_add(1, std::memory_order_relaxed);
    int64_t now = epochMs();
    int64_t last = lastRateLimitEventMs_.load();
    if (now - last >= 1000 && lastRateLimitEventMs_.compare_exchange_strong(last, now))
    {
        uint64_t count = rateLimitedSinceEvent_.exchange(0);
        recordEvent("rateLimit", std::to_string(count) +
                                     " request(s) rejected with 429; last client " + clientId);
    }
}

std::shared_ptr<common::TokenBucket> EdgeServer::bucketFor(const std::string& clientId)
{
    std::lock_guard<std::mutex> lock(clientsMutex_);
    steady_clock::time_point now = steady_clock::now();
    auto it = clients_.find(clientId);
    if (it == clients_.end() && clients_.size() >= config_.maxTrackedClients)
    {
        for (auto entry = clients_.begin(); entry != clients_.end();)
        {
            entry = now - entry->second.lastSeen > seconds(60) ? clients_.erase(entry)
                                                               : std::next(entry);
        }
        if (clients_.size() >= config_.maxTrackedClients)
        {
            clients_.erase(std::min_element(clients_.begin(), clients_.end(),
                                            [](const auto& left, const auto& right) {
                                                return left.second.lastSeen < right.second.lastSeen;
                                            }));
        }
    }
    if (it == clients_.end())
    {
        auto bucket =
            std::make_shared<common::TokenBucket>(config_.rateLimitBurst, config_.rateLimitPerSec);
        it = clients_.emplace(clientId, ClientBucket{bucket, now}).first;
    }
    it->second.lastSeen = now;
    return it->second.bucket;
}

}  // namespace edge
}  // namespace outpost
