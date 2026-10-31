/// @file main.cpp
/// @brief Outpost Write-API Service — validates and proxies requests to storage.
///
/// Sits between edge nodes and the storage service. Edges call write-api,
/// which validates the request and proxies it to storage. The storage service
/// is never exposed directly to edges.
///
/// Configuration via environment variables:
///   PORT          — listen port (default: 8090)
///   STORAGE_HOST  — storage service hostname (default: "storage")
///   STORAGE_PORT  — storage service port (default: 8091)

#include <cstdlib>
#include <string>

#include <httplib.h>
#include <spdlog/spdlog.h>

#include "writeApi/apiHandlers.h"
#include "writeApi/storageClient.h"

int main() {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [write-api] %v");
    spdlog::set_level(spdlog::level::info);

    const char* envPort = std::getenv("PORT");
    int port = envPort ? std::atoi(envPort) : 8090;

    const char* envStorageHost = std::getenv("STORAGE_HOST");
    std::string storageHost = envStorageHost ? envStorageHost : "storage";

    const char* envStoragePort = std::getenv("STORAGE_PORT");
    int storagePort = envStoragePort ? std::atoi(envStoragePort) : 8091;

    outpost::writeApi::StorageClient storageClient(storageHost, storagePort);
    httplib::Server server;

    outpost::writeApi::registerHandlers(server, storageClient);

    spdlog::info("Starting Outpost Write-API service on port {} (storage -> {}:{})", port,
                 storageHost, storagePort);

    if (!server.listen("0.0.0.0", port)) {
        spdlog::error("Failed to bind to port {}", port);
        return 1;
    }

    return 0;
}
