/// @file apiHandlers.cpp
/// @brief Write-API HTTP route handlers — validates and proxies to storage.

#include "writeApi/apiHandlers.h"

#include <atomic>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

using json = nlohmann::json;

namespace outpost {
namespace writeApi {

// Internal request counters for /debug/stats.
static std::atomic<uint64_t> s_totalGets{0};
static std::atomic<uint64_t> s_totalPuts{0};
static std::atomic<uint64_t> s_totalDeletes{0};
static std::atomic<uint64_t> s_totalErrors{0};

void registerHandlers(httplib::Server& server, StorageClient& client) {
    // ── Health check ────────────────────────────────────────────────────
    server.Get("/health", [](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"status", "healthy"}, {"service", "write-api"}};
        res.set_content(body.dump(), "application/json");
    });

    // ── GET /key/:id — proxy read to storage ────────────────────────────
    server.Get(R"(/key/(.+))",
               [&client](const httplib::Request& req, httplib::Response& res) {
                   std::string key = req.matches[1].str();
                   s_totalGets.fetch_add(1, std::memory_order_relaxed);

                   auto value = client.get(key);
                   if (!value.has_value()) {
                       json body = {{"error", "not_found"}, {"key", key}};
                       res.status = 404;
                       res.set_content(body.dump(), "application/json");
                       return;
                   }

                   json body = {{"key", key}, {"value", value.value()}};
                   res.set_content(body.dump(), "application/json");
               });

    // ── PUT /key/:id — validate then proxy write to storage ─────────────
    server.Put(R"(/key/(.+))",
               [&client](const httplib::Request& req, httplib::Response& res) {
                   std::string key = req.matches[1].str();
                   s_totalPuts.fetch_add(1, std::memory_order_relaxed);

                   // Validate: key must not be empty (regex already ensures non-empty)
                   // Validate: key must not exceed 256 bytes
                   if (key.size() > 256) {
                       json body = {{"error", "bad_request"},
                                    {"message", "Key exceeds 256 byte limit"}};
                       res.status = 400;
                       res.set_content(body.dump(), "application/json");
                       s_totalErrors.fetch_add(1, std::memory_order_relaxed);
                       return;
                   }

                   // Parse value from body
                   std::string value;
                   if (!req.body.empty()) {
                       try {
                           auto parsed = json::parse(req.body);
                           if (parsed.contains("value") && parsed["value"].is_string()) {
                               value = parsed["value"].get<std::string>();
                           } else {
                               json errBody = {{"error", "bad_request"},
                                               {"message", "JSON body must contain a string "
                                                           "\"value\" field"}};
                               res.status = 400;
                               res.set_content(errBody.dump(), "application/json");
                               s_totalErrors.fetch_add(1, std::memory_order_relaxed);
                               return;
                           }
                       } catch (const json::parse_error&) {
                           value = req.body;
                       }
                   } else {
                       json body = {{"error", "bad_request"},
                                    {"message", "Request body is required"}};
                       res.status = 400;
                       res.set_content(body.dump(), "application/json");
                       s_totalErrors.fetch_add(1, std::memory_order_relaxed);
                       return;
                   }

                   // Validate: value must not exceed 1 MB
                   if (value.size() > 1024 * 1024) {
                       json body = {{"error", "bad_request"},
                                    {"message", "Value exceeds 1MB limit"}};
                       res.status = 400;
                       res.set_content(body.dump(), "application/json");
                       s_totalErrors.fetch_add(1, std::memory_order_relaxed);
                       return;
                   }

                   auto storageRes = client.put(key, value);
                   if (!storageRes.success) {
                       json body = {{"error", "upstream_error"},
                                    {"message", storageRes.errorMessage}};
                       res.status = 502;
                       res.set_content(body.dump(), "application/json");
                       s_totalErrors.fetch_add(1, std::memory_order_relaxed);
                       return;
                   }

                   // Pass through the storage response
                   res.set_content(storageRes.body, "application/json");
               });

    // ── DELETE /key/:id — proxy delete to storage ───────────────────────
    server.Delete(R"(/key/(.+))",
                  [&client](const httplib::Request& req, httplib::Response& res) {
                      std::string key = req.matches[1].str();
                      s_totalDeletes.fetch_add(1, std::memory_order_relaxed);

                      auto storageRes = client.remove(key);
                      if (!storageRes.success) {
                          res.status = storageRes.httpStatus > 0 ? storageRes.httpStatus : 502;
                          res.set_content(storageRes.body.empty()
                                              ? json({{"error", "upstream_error"}}).dump()
                                              : storageRes.body,
                                          "application/json");
                          return;
                      }

                      res.set_content(storageRes.body, "application/json");
                  });

    // ── GET /debug/stats — service diagnostics ──────────────────────────
    server.Get("/debug/stats", [&client](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"service", "write-api"},
                     {"totalGets", s_totalGets.load(std::memory_order_relaxed)},
                     {"totalPuts", s_totalPuts.load(std::memory_order_relaxed)},
                     {"totalDeletes", s_totalDeletes.load(std::memory_order_relaxed)},
                     {"totalErrors", s_totalErrors.load(std::memory_order_relaxed)},
                     {"storageHealthy", client.isHealthy()}};
        res.set_content(body.dump(), "application/json");
    });

    spdlog::info("Write-API handlers registered: /health, /key/:id (GET/PUT/DELETE), /debug/stats");
}

}  // namespace writeApi
}  // namespace outpost
