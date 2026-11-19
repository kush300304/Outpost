#pragma once

/// @file storageClient.h
/// @brief HTTP client for the write-api service to communicate with the storage service.
///
/// Wraps cpp-httplib client calls to the storage service. Provides typed
/// get/put operations that serialize to/from JSON.

#include <cstdint>
#include <optional>
#include <string>

namespace outpost {
namespace writeApi {

/// @brief Result of a storage operation.
struct StorageResponse {
    int httpStatus = 0;
    std::string body;
    bool success = false;
    std::string errorMessage;
};

/// @brief HTTP client wrapper for communicating with the Outpost storage service.
///
/// Encapsulates host/port configuration and provides typed methods for
/// GET and versioned PUT operations on the storage KV API.
class StorageClient {
public:
    /// @param host Storage service hostname (e.g., "storage" in Docker network).
    /// @param port Storage service port (e.g., 8091).
    StorageClient(const std::string& host, int port);

    /// @brief Retrieve a value from the storage service.
    /// @param key The key to look up.
    /// @return The value if found, or std::nullopt if 404 or error.
    std::optional<std::string> get(const std::string& key);

    /// @brief Store a versioned key-value pair in the storage service.
    /// @param key      The key.
    /// @param value    The value.
    /// @param version  Write version (last-writer-wins ordering).
    /// @param writerId Identity of the writer that produced the version.
    /// @return StorageResponse with the result; success means storage answered 200.
    StorageResponse put(const std::string& key, const std::string& value, uint64_t version,
                        const std::string& writerId);

    /// @brief Check if the storage service is healthy.
    /// @return true if /health returns 200.
    bool isHealthy();

private:
    std::string host_;
    int port_;
};

}  // namespace writeApi
}  // namespace outpost
