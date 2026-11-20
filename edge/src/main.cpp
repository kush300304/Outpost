/// @file main.cpp
/// @brief Outpost edge service: caching proxy with circuit breaker and degraded mode.
///
/// Configuration via environment variables:
///   PORT            HTTP listen port (default 8081)
///   NODE_ID         unique node identifier (default "edge-1")
///   WRITE_API_HOST  write-api hostname (default "write-api")
///   WRITE_API_PORT  write-api port (default 9090)
///   CACHE_CAPACITY  LRU cache max entries (default 1024)
///   WAL_PATH        degraded-mode write queue (default /data/<nodeId>_write_queue.wal)
///   WAL_FSYNC       "0" disables fsync per queued write (default "1")
///   BREAKER_FAILURE_THRESHOLD  consecutive failures that open the breaker (default 3)
///   BREAKER_RESET_TIMEOUT_MS   OPEN time before the half-open probe (default 5000)
///   HEALTH_CHECK_INTERVAL_MS   time between write-api health checks (default 1000)
///   ORIGIN_TIMEOUT_MS          connect/read timeout for write-api calls (default 2000)
///   RATE_LIMIT_PER_SEC         token refill per client per second (default 100)
///   RATE_LIMIT_BURST           token bucket capacity per client (default 200)
///   MAX_TRACKED_CLIENTS        client buckets kept before eviction (default 10000)

#include <chrono>
#include <cstdlib>
#include <string>

#include <spdlog/spdlog.h>

#include "edge/edgeServer.h"

int main() {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [edge] %v");
    spdlog::set_level(spdlog::level::info);

    auto env = [](const char* name, const std::string& fallback) {
        const char* value = std::getenv(name);
        return value != nullptr ? std::string(value) : fallback;
    };

    outpost::edge::EdgeConfig config;
    config.port = std::stoi(env("PORT", "8081"));
    config.nodeId = env("NODE_ID", "edge-1");
    config.writeApiHost = env("WRITE_API_HOST", "write-api");
    config.writeApiPort = std::stoi(env("WRITE_API_PORT", "9090"));
    config.cacheCapacity = std::stoul(env("CACHE_CAPACITY", "1024"));
    config.walPath = env("WAL_PATH", "");
    config.walFsync = env("WAL_FSYNC", "1") != "0";
    config.breakerFailureThreshold = std::stoi(env("BREAKER_FAILURE_THRESHOLD", "3"));
    config.breakerResetTimeout =
        std::chrono::milliseconds(std::stol(env("BREAKER_RESET_TIMEOUT_MS", "5000")));
    config.healthCheckInterval =
        std::chrono::milliseconds(std::stol(env("HEALTH_CHECK_INTERVAL_MS", "1000")));
    config.originTimeout = std::chrono::milliseconds(std::stol(env("ORIGIN_TIMEOUT_MS", "2000")));
    config.rateLimitPerSec = std::stod(env("RATE_LIMIT_PER_SEC", "100"));
    config.rateLimitBurst = std::stoul(env("RATE_LIMIT_BURST", "200"));
    config.maxTrackedClients = std::stoul(env("MAX_TRACKED_CLIENTS", "10000"));

    outpost::edge::EdgeServer server(config);
    bool served = server.bind() > 0 && server.serve();
    return served ? 0 : 1;
}
