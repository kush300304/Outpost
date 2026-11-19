/// @file originClient.cpp
/// @brief HTTP client from an edge to write-api, with a timeout on every call.

#include "edge/originClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace outpost {
namespace edge {

OriginClient::OriginClient(const std::string& host, int port, std::chrono::milliseconds timeout)
    : host_(host), port_(port), timeout_(timeout) {
    spdlog::info("OriginClient configured: {}:{}", host_, port_);
}

std::optional<std::string> OriginClient::get(const std::string& key) {
    ++totalRequests_;
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(timeout_);
    cli.set_read_timeout(timeout_);

    std::optional<std::string> value;
    auto res = cli.Get("/key/" + key);
    if (!res) {
        ++failedRequests_;
        spdlog::error("OriginClient GET /key/{}: connection failed ({})", key,
                      httplib::to_string(res.error()));
    } else if (res->status == 200) {
        json parsed = json::parse(res->body, nullptr, false);
        if (parsed.is_object() && parsed.contains("value") && parsed["value"].is_string()) {
            value = parsed["value"].get<std::string>();
        } else {
            spdlog::error("OriginClient GET /key/{}: malformed response body", key);
        }
    } else if (res->status != 404) {
        ++failedRequests_;
    }
    return value;
}

OriginResponse OriginClient::put(const std::string& key, const std::string& value,
                                 uint64_t version, const std::string& writerId) {
    ++totalRequests_;
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(timeout_);
    cli.set_read_timeout(timeout_);

    json body = {{"value", value}, {"version", version}, {"writerId", writerId}};
    auto res = cli.Put("/key/" + key, body.dump(), "application/json");

    OriginResponse response;
    if (!res) {
        ++failedRequests_;
        response.errorMessage =
            "Connection failed: " + std::string(httplib::to_string(res.error()));
        spdlog::error("OriginClient PUT /key/{}: {}", key, response.errorMessage);
    } else {
        response.httpStatus = res->status;
        response.body = res->body;
        response.success = res->status == 200;
        if (!response.success) {
            ++failedRequests_;
            response.errorMessage = "Origin returned HTTP " + std::to_string(res->status);
        }
    }
    return response;
}

bool OriginClient::healthCheck() {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(timeout_);
    cli.set_read_timeout(timeout_);
    auto res = cli.Get("/health");
    return res && res->status == 200;
}

}  // namespace edge
}  // namespace outpost
