/// @file storageClient.cpp
/// @brief HTTP client from write-api to the storage service, with a timeout on every call.

#include "writeApi/storageClient.h"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "common/urlEncode.h"

using json = nlohmann::json;

namespace outpost {
namespace writeApi {

StorageClient::StorageClient(const std::string& host, int port) : host_(host), port_(port) {
    spdlog::info("StorageClient configured: {}:{}", host_, port_);
}

StorageResponse StorageClient::get(const std::string& key) {
    httplib::Client cli(host_, port_);
    cli.set_url_encode(false);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(5);

    StorageResponse response;
    auto res = cli.Get("/key/" + common::encodePathSegment(key));
    if (!res) {
        response.errorMessage =
            "Connection failed: " + std::string(httplib::to_string(res.error()));
        spdlog::error("StorageClient GET /key/{}: {}", key, response.errorMessage);
    } else {
        response.httpStatus = res->status;
        response.body = res->body;
        response.success = res->status == 200;
    }
    return response;
}

StorageResponse StorageClient::put(const std::string& key, const std::string& value,
                                   uint64_t version, const std::string& writerId) {
    httplib::Client cli(host_, port_);
    cli.set_url_encode(false);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(5);

    json body = {{"value", value}, {"version", version}, {"writerId", writerId}};
    auto res = cli.Put("/key/" + common::encodePathSegment(key), body.dump(), "application/json");

    StorageResponse response;
    if (!res) {
        response.errorMessage =
            "Connection failed: " + std::string(httplib::to_string(res.error()));
        spdlog::error("StorageClient PUT /key/{}: {}", key, response.errorMessage);
    } else {
        response.httpStatus = res->status;
        response.body = res->body;
        response.success = res->status == 200;
        if (!response.success) {
            response.errorMessage = "Storage returned HTTP " + std::to_string(res->status);
            spdlog::warn("StorageClient PUT /key/{}: {}", key, response.errorMessage);
        }
    }
    return response;
}

bool StorageClient::isHealthy() {
    httplib::Client cli(host_, port_);
    cli.set_url_encode(false);
    cli.set_connection_timeout(1);
    cli.set_read_timeout(1);
    auto res = cli.Get("/health");
    return res && res->status == 200;
}

}  // namespace writeApi
}  // namespace outpost
