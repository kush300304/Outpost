/// @file originClient.cpp
/// @brief HTTP client from an edge to write-api, with a timeout on every call.

#include "edge/originClient.h"

#include <httplib.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>

#include <nlohmann/json.hpp>

#include "common/urlEncode.h"

using json = nlohmann::json;

namespace outpost
{
namespace edge
{

OriginClient::OriginClient(const std::string& host, int port, std::chrono::milliseconds timeout)
    : host_(host), port_(port), timeout_(timeout)
{
    spdlog::info("OriginClient configured: {}:{}", host_, port_);
}

OriginResponse OriginClient::get(const std::string& key)
{
    ++totalRequests_;
    httplib::Client cli(host_, port_);
    cli.set_address_family(AF_INET);
    cli.set_url_encode(false);
    cli.set_connection_timeout(timeout_);
    cli.set_read_timeout(timeout_);

    OriginResponse response;
    auto res = cli.Get("/key/" + common::encodePathSegment(key));
    if (!res)
    {
        ++failedRequests_;
        response.errorMessage =
            "Connection failed: " + std::string(httplib::to_string(res.error()));
        spdlog::error("OriginClient GET /key/{}: {}", key, response.errorMessage);
    }
    else
    {
        response.httpStatus = res->status;
        response.body = res->body;
        response.success = res->status == 200;
        failedRequests_ += (res->status == 200 || res->status == 404) ? 0 : 1;
    }
    return response;
}

OriginResponse OriginClient::put(const std::string& key, const std::string& value, uint64_t version,
                                 const std::string& writerId)
{
    ++totalRequests_;
    httplib::Client cli(host_, port_);
    cli.set_address_family(AF_INET);
    cli.set_url_encode(false);
    cli.set_connection_timeout(timeout_);
    cli.set_read_timeout(timeout_);

    json body = {{"value", value}, {"version", version}, {"writerId", writerId}};
    auto res = cli.Put("/key/" + common::encodePathSegment(key), body.dump(), "application/json");

    OriginResponse response;
    if (!res)
    {
        ++failedRequests_;
        response.errorMessage =
            "Connection failed: " + std::string(httplib::to_string(res.error()));
        spdlog::error("OriginClient PUT /key/{}: {}", key, response.errorMessage);
    }
    else
    {
        response.httpStatus = res->status;
        response.body = res->body;
        response.success = res->status == 200;
        if (!response.success)
        {
            ++failedRequests_;
            response.errorMessage = "Origin returned HTTP " + std::to_string(res->status);
        }
    }
    return response;
}

bool OriginClient::healthCheck()
{
    httplib::Client cli(host_, port_);
    cli.set_address_family(AF_INET);
    cli.set_url_encode(false);
    cli.set_connection_timeout(timeout_);
    cli.set_read_timeout(timeout_);
    auto res = cli.Get("/health");
    return res && res->status == 200;
}

}  // namespace edge
}  // namespace outpost
