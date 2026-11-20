/// @file apiHandlers.cpp
/// @brief Write-API HTTP route handlers — validates and proxies to storage.

#include "writeApi/apiHandlers.h"

#include <spdlog/spdlog.h>

#include <atomic>
#include <nlohmann/json.hpp>
#include <string>

#include "common/requestLimits.h"

using json = nlohmann::json;

namespace outpost
{
namespace writeApi
{

// Internal request counters for /debug/stats.
static std::atomic<uint64_t> s_totalGets{0};
static std::atomic<uint64_t> s_totalPuts{0};
static std::atomic<uint64_t> s_totalErrors{0};

void registerHandlers(httplib::Server& server, StorageClient& client)
{
    // ── Health: healthy only when storage answers /health within 1 s ────
    server.Get("/health", [&client](const httplib::Request& /*req*/, httplib::Response& res) {
        bool storageUp = client.isHealthy();
        res.status = storageUp ? 200 : 503;
        json body = {{"status", storageUp ? "healthy" : "unhealthy"},
                     {"service", "write-api"},
                     {"storage", storageUp ? "up" : "down"}};
        res.set_content(body.dump(), "application/json");
    });

    // ── GET /key/:id — proxy read to storage ────────────────────────────
    server.Get(R"(/key/(.+))", [&client](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        s_totalGets.fetch_add(1, std::memory_order_relaxed);

        StorageResponse storageRes = client.get(key);
        std::string content = storageRes.body;
        if (storageRes.httpStatus == 404)
        {
            res.status = 404;
            content = json({{"error", "not_found"}, {"key", key}}).dump();
        }
        else if (!storageRes.success)
        {
            res.status = 502;
            content = json({{"error", "upstream_error"},
                            {"message",
                             storageRes.errorMessage.empty()
                                 ? "storage returned HTTP " + std::to_string(storageRes.httpStatus)
                                 : storageRes.errorMessage}})
                          .dump();
            s_totalErrors.fetch_add(1, std::memory_order_relaxed);
        }
        res.set_content(content, "application/json");
    });

    // ── PUT /key/:id — validate then proxy the versioned write to storage ─
    server.Put(R"(/key/(.+))", [&client](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        s_totalPuts.fetch_add(1, std::memory_order_relaxed);
        json request = json::parse(req.body, nullptr, false);

        std::string error;
        if (key.size() > common::kMaxKeyBytes)
        {
            error = "key exceeds 256 bytes";
        }
        else if (!request.is_object() || !request.contains("value") ||
                 !request["value"].is_string())
        {
            error = "body must be JSON with a string \"value\"";
        }
        else if (request["value"].get_ref<const std::string&>().size() > common::kMaxValueBytes)
        {
            error = "value exceeds 1 MiB";
        }
        else if (!request.contains("version") || !request["version"].is_number_unsigned() ||
                 request["version"].get<uint64_t>() == 0)
        {
            error = "\"version\" must be an unsigned integer > 0";
        }
        else if (!request.contains("writerId") || !request["writerId"].is_string() ||
                 request["writerId"].get_ref<const std::string&>().empty())
        {
            error = "\"writerId\" must be a non-empty string";
        }

        std::string content;
        if (!error.empty())
        {
            res.status = 400;
            content = json({{"error", "bad_request"}, {"message", error}}).dump();
            s_totalErrors.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            StorageResponse storageRes = client.put(key, request["value"].get<std::string>(),
                                                    request["version"].get<uint64_t>(),
                                                    request["writerId"].get<std::string>());
            if (storageRes.success)
            {
                content = storageRes.body;
            }
            else
            {
                res.status = 502;
                content = json({{"error", "upstream_error"}, {"message", storageRes.errorMessage}})
                              .dump();
                s_totalErrors.fetch_add(1, std::memory_order_relaxed);
            }
        }
        res.set_content(content, "application/json");
    });

    // ── GET /debug/stats — service diagnostics ──────────────────────────
    server.Get("/debug/stats", [&client](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"service", "write-api"},
                     {"totalGets", s_totalGets.load(std::memory_order_relaxed)},
                     {"totalPuts", s_totalPuts.load(std::memory_order_relaxed)},
                     {"totalErrors", s_totalErrors.load(std::memory_order_relaxed)},
                     {"storageHealthy", client.isHealthy()}};
        res.set_content(body.dump(), "application/json");
    });

    spdlog::info("Write-API handlers registered: /health, /key/:id (GET/PUT), /debug/stats");
}

}  // namespace writeApi
}  // namespace outpost
