/// @file main.cpp
/// @brief Outpost Storage Service — authoritative key-value data store.
///
/// Exposes an HTTP API for the write-api service to read and write key-value
/// pairs. Edges never reach this service directly; only write-api talks to it.
///
/// Endpoints:
///   GET  /health        → 200 {"status":"healthy","service":"storage"}
///   GET  /key/:id       → 200 {"key":"...","value":"..."} or 404
///   PUT  /key/:id       → 200 {"key":"...","value":"...","created":bool}
///   DELETE /key/:id     → 200 {"key":"...","deleted":true} or 404
///   GET  /debug/stats   → 200 {"totalKeys":N}
///   GET  /debug/keys    → 200 {"keys":["a","b",...]}

#include <cstdlib>
#include <string>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include "storage/kvStore.h"

using json = nlohmann::json;

int main() {
    spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [storage] %v");
    spdlog::set_level(spdlog::level::info);

    const char* envPort = std::getenv("PORT");
    int port = envPort ? std::atoi(envPort) : 8091;

    // WAL path for crash-recoverable persistence. Defaults to /data/storage.wal
    // inside the container, configurable via WAL_PATH env var.
    const char* envWalPath = std::getenv("WAL_PATH");
    std::string walPath = envWalPath ? envWalPath : "/data/storage.wal";

    spdlog::info("Initializing KvStore with WAL at: {}", walPath);
    outpost::storage::KvStore store(walPath);
    httplib::Server server;

    // ── Health check ────────────────────────────────────────────────────
    server.Get("/health", [](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"status", "healthy"}, {"service", "storage"}};
        res.set_content(body.dump(), "application/json");
    });

    // ── GET /key/:id — retrieve a value ─────────────────────────────────
    server.Get(R"(/key/(.+))",
               [&store](const httplib::Request& req, httplib::Response& res) {
                   std::string key = req.matches[1].str();

                   auto value = store.get(key);
                   if (!value.has_value()) {
                       json body = {{"error", "not_found"}, {"key", key}};
                       res.status = 404;
                       res.set_content(body.dump(), "application/json");
                       return;
                   }

                   json body = {{"key", key}, {"value", value.value()}};
                   res.set_content(body.dump(), "application/json");
                   spdlog::debug("GET /key/{} -> found", key);
               });

    // ── PUT /key/:id — create or update a value ─────────────────────────
    server.Put(R"(/key/(.+))",
               [&store](const httplib::Request& req, httplib::Response& res) {
                   std::string key = req.matches[1].str();

                   // Accept JSON body with a "value" field, or use raw body as the value
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
                               return;
                           }
                       } catch (const json::parse_error&) {
                           // Treat non-JSON body as raw string value
                           value = req.body;
                       }
                   } else {
                       json errBody = {{"error", "bad_request"},
                                       {"message", "Request body is required"}};
                       res.status = 400;
                       res.set_content(errBody.dump(), "application/json");
                       return;
                   }

                   bool existed = store.exists(key);
                   store.put(key, value);

                   json body = {{"key", key}, {"value", value}, {"created", !existed}};
                   res.set_content(body.dump(), "application/json");
                   spdlog::info("PUT /key/{} -> {} ({} bytes)", key,
                                existed ? "updated" : "created", value.size());
               });

    // ── DELETE /key/:id — remove a value ────────────────────────────────
    server.Delete(R"(/key/(.+))",
                  [&store](const httplib::Request& req, httplib::Response& res) {
                      std::string key = req.matches[1].str();

                      if (!store.remove(key)) {
                          json body = {{"error", "not_found"}, {"key", key}};
                          res.status = 404;
                          res.set_content(body.dump(), "application/json");
                          return;
                      }

                      json body = {{"key", key}, {"deleted", true}};
                      res.set_content(body.dump(), "application/json");
                      spdlog::info("DELETE /key/{} -> removed", key);
                  });

    // ── GET /debug/stats — diagnostic info ──────────────────────────────
    server.Get("/debug/stats", [&store](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"service", "storage"},
                     {"totalKeys", store.size()},
                     {"durable", store.isDurable()}};
        res.set_content(body.dump(), "application/json");
    });

    // ── GET /debug/keys — list all keys ─────────────────────────────────
    server.Get("/debug/keys", [&store](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"keys", store.getAllKeys()}};
        res.set_content(body.dump(), "application/json");
    });

    spdlog::info("Starting Outpost Storage service on port {}", port);
    if (!server.listen("0.0.0.0", port)) {
        spdlog::error("Failed to bind to port {}", port);
        return 1;
    }

    return 0;
}
