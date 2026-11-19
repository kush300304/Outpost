/// @file main.cpp
/// @brief Outpost storage service: owns the authoritative versioned key-value data.
///
/// Configuration via environment variables:
///   PORT       listen port (default 9091)
///   WAL_PATH   write-ahead log file (default /data/storage.wal)
///   WAL_FSYNC  "0" disables fsync per applied write (default "1")

#include <cstdlib>
#include <string>

#include <httplib.h>
#include <spdlog/spdlog.h>

#include "storage/kvStore.h"
#include "storage/storageHandlers.h"

int main() {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [storage] %v");
    spdlog::set_level(spdlog::level::info);

    const char* envPort = std::getenv("PORT");
    const char* envWalPath = std::getenv("WAL_PATH");
    const char* envFsync = std::getenv("WAL_FSYNC");
    int port = envPort ? std::atoi(envPort) : 9091;
    std::string walPath = envWalPath ? envWalPath : "/data/storage.wal";
    bool fsyncOnAppend = envFsync == nullptr || std::string(envFsync) != "0";

    outpost::storage::KvStore store(walPath, fsyncOnAppend);
    outpost::common::WalRecoveryReport recovery = store.recoveryReport();
    spdlog::info("WAL {} recovered: {} records, {} keys, {} damaged bytes skipped, {} tail bytes "
                 "truncated (fsync={})",
                 walPath, recovery.validRecords, store.size(), recovery.skippedBytes,
                 recovery.truncatedBytes, fsyncOnAppend);

    httplib::Server server;
    outpost::storage::registerStorageRoutes(server, store);
    spdlog::info("Starting storage service on port {}", port);
    bool listened = server.listen("0.0.0.0", port);
    if (!listened) {
        spdlog::error("Failed to bind to port {}", port);
    }
    return listened ? 0 : 1;
}
