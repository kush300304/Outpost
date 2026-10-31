/// @file main.cpp
/// @brief Outpost Edge Service — caching proxy with circuit breaker.
///
/// Each edge node runs one instance of this binary. Configuration is via
/// environment variables:
///   PORT              — HTTP listen port (default: 8080)
///   NODE_ID           — unique node identifier (default: "edge-1")
///   WRITE_API_HOST    — write-api hostname (default: "write-api")
///   WRITE_API_PORT    — write-api port (default: 8090)
///   CACHE_CAPACITY    — LRU cache max entries (default: 1024)
///   WAL_PATH          — path for degraded-mode write queue (default: /data/<nodeId>_write_queue.wal)

#include <cstdlib>
#include <string>

#include <spdlog/spdlog.h>

#include "edge/edgeServer.h"

int main() {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [edge] %v");
    spdlog::set_level(spdlog::level::info);

    outpost::edge::EdgeConfig config;

    const char* envPort = std::getenv("PORT");
    config.port = envPort ? std::atoi(envPort) : 8080;

    const char* envNodeId = std::getenv("NODE_ID");
    config.nodeId = envNodeId ? envNodeId : "edge-1";

    const char* envWriteApiHost = std::getenv("WRITE_API_HOST");
    config.writeApiHost = envWriteApiHost ? envWriteApiHost : "write-api";

    const char* envWriteApiPort = std::getenv("WRITE_API_PORT");
    config.writeApiPort = envWriteApiPort ? std::atoi(envWriteApiPort) : 8090;

    const char* envCacheCapacity = std::getenv("CACHE_CAPACITY");
    config.cacheCapacity = envCacheCapacity ? std::stoul(envCacheCapacity) : 1024;

    const char* envWalPath = std::getenv("WAL_PATH");
    if (envWalPath) {
        config.walPath = envWalPath;
    }

    spdlog::info("Starting Outpost Edge [{}] on port {} (write-api -> {}:{})", config.nodeId,
                 config.port, config.writeApiHost, config.writeApiPort);

    outpost::edge::EdgeServer server(config);
    server.run();

    return 0;
}
