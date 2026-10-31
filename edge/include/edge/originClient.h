#pragma once

/// @file originClient.h
/// @brief HTTP client for edge nodes to communicate with the write-api service.
///
/// Encapsulates all upstream communication. In later phases, the circuit
/// breaker wraps calls through this client — when the breaker opens, the
/// edge serves stale cache instead of calling through here.

#include <cstdint>
#include <optional>
#include <string>

namespace outpost {
namespace edge {

/// @brief Result of an origin (write-api) operation.
struct OriginResponse {
    int httpStatus = 0;
    std::string body;
    bool success = false;
    std::string errorMessage;
};

/// @brief HTTP client for upstream communication from edge to write-api.
class OriginClient {
public:
    /// @param host Write-API hostname (e.g., "write-api" in Docker network).
    /// @param port Write-API port (e.g., 8090).
    OriginClient(const std::string& host, int port);

    /// @brief Fetch a value from the origin via write-api.
    /// @param key The key to look up.
    /// @return The value if found, or std::nullopt on 404/error.
    std::optional<std::string> get(const std::string& key);

    /// @brief Store a key-value pair at the origin via write-api.
    /// @param key   The key.
    /// @param value The value.
    /// @return OriginResponse with the result.
    OriginResponse put(const std::string& key, const std::string& value);

    /// @brief Check if the write-api is reachable and healthy.
    /// @return true if /health returns 200.
    bool healthCheck();

    /// @brief Return the total number of requests sent to the origin.
    uint64_t totalRequests() const { return totalRequests_; }

    /// @brief Return the total number of failed requests.
    uint64_t failedRequests() const { return failedRequests_; }

private:
    std::string host_;
    int port_;
    uint64_t totalRequests_ = 0;
    uint64_t failedRequests_ = 0;
};

}  // namespace edge
}  // namespace outpost
