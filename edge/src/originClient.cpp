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

    auto res = cli.Get("/key/" + key);
    if (!res) {
        ++failedRequests_;
        spdlog::error("OriginClient GET /key/{}: connection failed ({})", key,
                      httplib::to_string(res.error()));
        return std::nullopt;
    }

    if (res->status == 200) {
        try {
            auto parsed = json::parse(res->body);
            if (parsed.contains("value")) {
                return parsed["value"].get<std::string>();
            }
        } catch (const json::parse_error& e) {
            spdlog::error("OriginClient GET /key/{}: JSON parse error: {}", key, e.what());
        }
    }

    if (res->status != 404) {
        // 404 is expected (key not found), anything else is unexpected
        ++failedRequests_;
    }

    return std::nullopt;
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
        return response;
    }

    response.httpStatus = res->status;
    response.body = res->body;
    response.success = (res->status == 200);

    if (!response.success) {
        ++failedRequests_;
        response.errorMessage = "Origin returned HTTP " + std::to_string(res->status);
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
