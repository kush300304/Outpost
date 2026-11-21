/// @file main.cpp
/// @brief Outpost write-api service: validates writes and proxies reads and writes to storage.
///
/// Edges call write-api; write-api is the only client of the storage service.
///
/// Configuration via environment variables:
///   PORT          listen port (default 9090)
///   STORAGE_HOST  storage service hostname (default "storage")
///   STORAGE_PORT  storage service port (default 9091)
///   ADMIN_ENABLED "1" registers the simulated-outage admin endpoint (default "0")
///   LOG_LEVEL     trace, debug, info, warn, error, critical or off (default info)

#include <httplib.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <string>

#include "common/shutdownSignal.h"
#include "writeApi/apiHandlers.h"
#include "writeApi/storageClient.h"

int main()
{
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [write-api] %v");
    const char* levelSetting = std::getenv("LOG_LEVEL");
    std::string levelName = levelSetting != nullptr ? levelSetting : "info";
    spdlog::level::level_enum level = spdlog::level::from_str(levelName);
    bool knownLevel = level != spdlog::level::off || levelName == "off";
    spdlog::set_level(knownLevel ? level : spdlog::level::info);
    if (!knownLevel)
    {
        spdlog::warn("Unknown LOG_LEVEL '{}'; using info", levelName);
    }

    auto env = [](const char* name, const std::string& fallback) {
        const char* value = std::getenv(name);
        return value != nullptr ? std::string(value) : fallback;
    };
    int port = std::stoi(env("PORT", "9090"));
    std::string storageHost = env("STORAGE_HOST", "storage");
    int storagePort = std::stoi(env("STORAGE_PORT", "9091"));

    outpost::writeApi::StorageClient storageClient(storageHost, storagePort);
    httplib::Server server;
    outpost::writeApi::HandlerOptions options;
    options.adminEnabled = env("ADMIN_ENABLED", "0") == "1";
    outpost::writeApi::registerHandlers(server, storageClient, options);
    outpost::common::ShutdownSignal shutdown([&server] {
        spdlog::info("Shutdown signal received, stopping");
        server.stop();
    });

    spdlog::info("Starting write-api on port {} (storage -> {}:{})", port, storageHost,
                 storagePort);
    bool listened = server.listen("0.0.0.0", port);
    if (!listened)
    {
        spdlog::error("Failed to bind to port {}", port);
    }
    return listened ? 0 : 1;
}
