/// @file storageHandlers.cpp
/// @brief Storage service HTTP routes: versioned reads and writes over KvStore.

#include "storage/storageHandlers.h"

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
#include <string>

using json = nlohmann::json;

namespace outpost
{
namespace storage
{

void registerStorageRoutes(httplib::Server& server, KvStore& store)
{
    server.Get("/health", [](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"status", "healthy"}, {"service", "storage"}};
        res.set_content(body.dump(), "application/json");
    });

    server.Get(R"(/key/(.+))", [&store](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        auto stored = store.get(key);
        json body;
        if (stored)
        {
            body = {{"key", key},
                    {"value", stored->value},
                    {"version", stored->version},
                    {"writerId", stored->writerId}};
        }
        else
        {
            res.status = 404;
            body = {{"error", "not_found"}, {"key", key}};
        }
        res.set_content(body.dump(), "application/json");
    });

    server.Put(R"(/key/(.+))", [&store](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        json request = json::parse(req.body, nullptr, false);
        bool valid =
            request.is_object() && request.contains("value") && request["value"].is_string() &&
            request.contains("version") && request["version"].is_number_unsigned() &&
            request["version"].get<uint64_t>() > 0 && request.contains("writerId") &&
            request["writerId"].is_string() && !request["writerId"].get<std::string>().empty();
        json body;
        if (valid)
        {
            uint64_t version = request["version"].get<uint64_t>();
            std::string writerId = request["writerId"].get<std::string>();
            PutOutcome outcome =
                store.put(key, request["value"].get<std::string>(), version, writerId);
            auto stored = store.get(key);
            body = {{"key", key},
                    {"result", putOutcomeToString(outcome)},
                    {"version", stored ? stored->version : version},
                    {"writerId", stored ? stored->writerId : writerId}};
            spdlog::debug("PUT /key/{} v={} by {} -> {}", key, version, writerId,
                          putOutcomeToString(outcome));
        }
        else
        {
            res.status = 400;
            body = {{"error", "bad_request"},
                    {"message",
                     "body must be {\"value\":string,\"version\":uint>0,\"writerId\":string}"}};
        }
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/debug/stats", [&store](const httplib::Request& /*req*/, httplib::Response& res) {
        KvStore::Counters counters = store.counters();
        common::WalRecoveryReport recovery = store.recoveryReport();
        common::WalStats wal = store.walStats();
        json body = {{"service", "storage"},
                     {"totalKeys", store.size()},
                     {"durable", store.isDurable()},
                     {"puts",
                      {{"applied", counters.applied},
                       {"duplicates", counters.duplicates},
                       {"superseded", counters.superseded}}},
                     {"wal", {{"appends", wal.appends}, {"fsyncs", wal.fsyncs}}},
                     {"walRecovery",
                      {{"validRecords", recovery.validRecords},
                       {"skippedBytes", recovery.skippedBytes},
                       {"truncatedBytes", recovery.truncatedBytes}}}};
        res.set_content(body.dump(), "application/json");
    });

    server.Get("/debug/keys", [&store](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"keys", store.getAllKeys()}};
        res.set_content(body.dump(), "application/json");
    });
}

}  // namespace storage
}  // namespace outpost
