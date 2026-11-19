/// @file main.cpp
/// @brief Outpost write-api service: validates writes and proxies reads and writes to storage.
///
/// Edges call write-api; write-api is the only client of the storage service.
///
/// Configuration via environment variables:
///   PORT          listen port (default 9090)
///   STORAGE_HOST  storage service hostname (default "storage")
///   STORAGE_PORT  storage service port (default 9091)

#include <cstdlib>
#include <string>

#include <httplib.h>
#include <spdlog/spdlog.h>

#include "writeApi/apiHandlers.h"
#include "writeApi/storageClient.h"

int main() {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [write-api] %v");
    spdlog::set_level(spdlog::level::info);

    auto env = [](const char* name, const std::string& fallback) {
        const char* value = std::getenv(name);
        return value != nullptr ? std::string(value) : fallback;
    };
    int port = std::stoi(env("PORT", "9090"));
    std::string storageHost = env("STORAGE_HOST", "storage");
    int storagePort = std::stoi(env("STORAGE_PORT", "9091"));

    outpost::writeApi::StorageClient storageClient(storageHost, storagePort);
    httplib::Server server;
    outpost::writeApi::registerHandlers(server, storageClient);

    spdlog::info("Starting write-api on port {} (storage -> {}:{})", port, storageHost,
                 storagePort);
    bool listened = server.listen("0.0.0.0", port);
    if (!listened) {
        spdlog::error("Failed to bind to port {}", port);
    }
    return listened ? 0 : 1;
}
