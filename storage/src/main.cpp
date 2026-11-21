/// @file main.cpp
/// @brief Outpost storage service: owns the authoritative versioned key-value data.
///
/// Configuration via environment variables:
///   PORT       listen port (default 9091)
///   WAL_PATH   write-ahead log file (default /data/storage.wal)
///   WAL_FSYNC  "0" disables fsync per applied write (default "1")
///   LOG_LEVEL  trace, debug, info, warn, error, critical or off (default info)

#include <httplib.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <string>

#include "common/shutdownSignal.h"
#include "storage/kvStore.h"
#include "storage/storageHandlers.h"

int main()
{
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [storage] %v");
    const char* levelSetting = std::getenv("LOG_LEVEL");
    std::string levelName = levelSetting != nullptr ? levelSetting : "info";
    spdlog::level::level_enum level = spdlog::level::from_str(levelName);
    bool knownLevel = level != spdlog::level::off || levelName == "off";
    spdlog::set_level(knownLevel ? level : spdlog::level::info);
    if (!knownLevel)
    {
        spdlog::warn("Unknown LOG_LEVEL '{}'; using info", levelName);
    }

    const char* envPort = std::getenv("PORT");
    const char* envWalPath = std::getenv("WAL_PATH");
    const char* envFsync = std::getenv("WAL_FSYNC");
    int port = envPort ? std::atoi(envPort) : 9091;
    std::string walPath = envWalPath ? envWalPath : "/data/storage.wal";
    bool fsyncOnAppend = envFsync == nullptr || std::string(envFsync) != "0";

    outpost::storage::KvStore store(walPath, fsyncOnAppend);
    outpost::common::WalRecoveryReport recovery = store.recoveryReport();
    spdlog::info(
        "WAL {} recovered: {} records, {} keys, {} damaged bytes skipped, {} tail bytes "
        "truncated (fsync={})",
        walPath, recovery.validRecords, store.size(), recovery.skippedBytes,
        recovery.truncatedBytes, fsyncOnAppend);

    httplib::Server server;
    outpost::storage::registerStorageRoutes(server, store);
    outpost::common::ShutdownSignal shutdown([&server] {
        spdlog::info("Shutdown signal received, stopping");
        server.stop();
    });
    spdlog::info("Starting storage service on port {}", port);
    bool listened = server.listen("0.0.0.0", port);
    if (!listened)
    {
        spdlog::error("Failed to bind to port {}", port);
    }
    return listened ? 0 : 1;
}
