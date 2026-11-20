/// @file main.cpp
/// @brief Outpost router service: consistent-hash routing of client requests to edge nodes.
///
/// Configuration via environment variables:
///   PORT                      listen port (default 8080)
///   EDGE_NODES                comma-separated id=host:port list
///                             (default edge-1=edge-1:8081,edge-2=edge-2:8082,edge-3=edge-3:8083)
///   VIRTUAL_NODES             ring points per edge (default 150)
///   HEALTH_CHECK_INTERVAL_MS  time between edge health checks (default 1000)
///   EDGE_TIMEOUT_MS           connect/read timeout to an edge (default 2000)
///   UNHEALTHY_THRESHOLD       failed checks before an edge leaves the ring (default 2)

#include <chrono>
#include <cstdlib>
#include <sstream>
#include <string>

#include <spdlog/spdlog.h>

#include "router/routerServer.h"

int main() {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [router] %v");
    spdlog::set_level(spdlog::level::info);

    auto env = [](const char* name, const std::string& fallback) {
        const char* value = std::getenv(name);
        return value != nullptr ? std::string(value) : fallback;
    };

    outpost::router::RouterConfig config;
    config.port = std::stoi(env("PORT", "8080"));
    config.virtualNodes = std::stoul(env("VIRTUAL_NODES", "150"));
    config.healthCheckInterval =
        std::chrono::milliseconds(std::stol(env("HEALTH_CHECK_INTERVAL_MS", "1000")));
    config.edgeTimeout = std::chrono::milliseconds(std::stol(env("EDGE_TIMEOUT_MS", "2000")));
    config.unhealthyThreshold = std::stoi(env("UNHEALTHY_THRESHOLD", "2"));

    std::stringstream nodes(
        env("EDGE_NODES", "edge-1=edge-1:8081,edge-2=edge-2:8082,edge-3=edge-3:8083"));
    std::string entry;
    while (std::getline(nodes, entry, ',')) {
        std::size_t equals = entry.find('=');
        std::size_t colon = entry.rfind(':');
        if (equals != std::string::npos && colon != std::string::npos && colon > equals) {
            config.edges.push_back(outpost::router::EdgeNode{
                entry.substr(0, equals), entry.substr(equals + 1, colon - equals - 1),
                std::stoi(entry.substr(colon + 1))});
        } else {
            spdlog::error("Ignoring malformed EDGE_NODES entry '{}'", entry);
        }
    }

    outpost::router::RouterServer server(config);
    bool served = server.bind() > 0 && server.serve();
    return served ? 0 : 1;
}
