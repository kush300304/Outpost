#include "writeApi/storageClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace outpost {
namespace writeApi {

StorageClient::StorageClient(const std::string& host, int port) : host_(host), port_(port) {
    spdlog::info("StorageClient configured: {}:{}", host_, port_);
}

std::optional<std::string> StorageClient::get(const std::string& key) {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(5);

    auto res = cli.Get("/key/" + key);
    if (!res) {
        spdlog::error("StorageClient GET /key/{}: connection failed ({})", key,
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
            spdlog::error("StorageClient GET /key/{}: JSON parse error: {}", key, e.what());
        }
    }

    return std::nullopt;
}

StorageResponse StorageClient::put(const std::string& key, const std::string& value) {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(5);

    json body = {{"value", value}};
    auto res = cli.Put("/key/" + key, body.dump(), "application/json");

    StorageResponse response;
    if (!res) {
        response.errorMessage =
            "Connection failed: " + std::string(httplib::to_string(res.error()));
        spdlog::error("StorageClient PUT /key/{}: {}", key, response.errorMessage);
        return response;
    }

    response.httpStatus = res->status;
    response.body = res->body;
    response.success = (res->status == 200);

    if (!response.success) {
        response.errorMessage = "Storage returned HTTP " + std::to_string(res->status);
        spdlog::warn("StorageClient PUT /key/{}: {}", key, response.errorMessage);
    }

    return response;
}

StorageResponse StorageClient::remove(const std::string& key) {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(5);

    auto res = cli.Delete("/key/" + key);

    StorageResponse response;
    if (!res) {
        response.errorMessage =
            "Connection failed: " + std::string(httplib::to_string(res.error()));
        spdlog::error("StorageClient DELETE /key/{}: {}", key, response.errorMessage);
        return response;
    }

    response.httpStatus = res->status;
    response.body = res->body;
    response.success = (res->status == 200);
    return response;
}

bool StorageClient::isHealthy() {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(1);
    cli.set_read_timeout(1);

    auto res = cli.Get("/health");
    return res && res->status == 200;
}

}  // namespace writeApi
}  // namespace outpost
