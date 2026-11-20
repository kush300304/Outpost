#pragma once

/// @file originClient.h
/// @brief HTTP client for edge nodes to communicate with the write-api service.
///
/// Encapsulates all upstream communication. In later phases, the circuit
/// breaker wraps calls through this client — when the breaker opens, the
/// edge serves stale cache instead of calling through here.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace outpost
{
namespace edge
{

/// @brief Result of an origin (write-api) operation.
struct OriginResponse
{
    int httpStatus = 0;
    std::string body;
    bool success = false;
    std::string errorMessage;
};

/// @brief HTTP client for upstream communication from edge to write-api.
class OriginClient
{
public:
    /// @param host    Write-API hostname (e.g., "write-api" in the compose network).
    /// @param port    Write-API port.
    /// @param timeout Connect and read timeout for every call.
    OriginClient(const std::string& host, int port, std::chrono::milliseconds timeout);

    /// @brief Fetch a key from the origin via write-api.
    /// @return httpStatus 200 with the JSON body, 404 when absent, another status on an origin
    ///         error, or 0 with errorMessage when write-api could not be reached.
    OriginResponse get(const std::string& key);

    /// @brief Store a versioned key-value pair at the origin via write-api.
    /// @param key      The key.
    /// @param value    The value.
    /// @param version  Write version stamped by the accepting edge.
    /// @param writerId Node id of the accepting edge.
    /// @return OriginResponse with the result.
    OriginResponse put(const std::string& key, const std::string& value, uint64_t version,
                       const std::string& writerId);

    /// @brief Check if the write-api is reachable and healthy.
    /// @return true if /health returns 200.
    bool healthCheck();

    /// @brief Return the total number of requests sent to the origin.
    uint64_t totalRequests() const
    {
        return totalRequests_;
    }

    /// @brief Return the total number of failed requests.
    uint64_t failedRequests() const
    {
        return failedRequests_;
    }

private:
    std::string host_;
    int port_;
    std::chrono::milliseconds timeout_;
    std::atomic<uint64_t> totalRequests_{0};  ///< Updated from concurrent request threads.
    std::atomic<uint64_t> failedRequests_{0};
};

}  // namespace edge
}  // namespace outpost
