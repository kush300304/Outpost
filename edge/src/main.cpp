/// @file main.cpp
/// @brief Outpost edge service: caching proxy with degraded mode.
///
/// Configuration via environment variables:
///   PORT            HTTP listen port (default 8081)
///   NODE_ID         unique node identifier (default "edge-1")
///   WRITE_API_HOST  write-api hostname (default "write-api")
///   WRITE_API_PORT  write-api port (default 9090)
///   CACHE_CAPACITY  LRU cache max entries (default 1024)
///   WAL_PATH        degraded-mode write queue (default /data/<nodeId>_write_queue.wal)

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

    outpost::edge::EdgeServer server(config);
    bool served = server.bind() > 0 && server.serve();
    return served ? 0 : 1;
}
