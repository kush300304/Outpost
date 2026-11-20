/// @file testApiHandlers.cpp
/// @brief Tests for the write-api HTTP handlers.
///
/// Every test runs the real write-api handlers on an ephemeral port, in front of either real
/// in-process storage handlers or an unreachable storage port, and talks to them over HTTP.

#include <gtest/gtest.h>
#include <httplib.h>

#include <chrono>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>

#include "storage/kvStore.h"
#include "storage/storageHandlers.h"
#include "writeApi/apiHandlers.h"
#include "writeApi/storageClient.h"

using json = nlohmann::json;

namespace
{

/// An httplib server running on an ephemeral loopback port for the lifetime of the object.
class BackgroundServer
{
public:
    ~BackgroundServer()
    {
        server.stop();
        if (thread_.joinable())
        {
            thread_.join();
        }
    }
    void start()
    {
        port_ = server.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    int port() const
    {
        return port_;
    }
    httplib::Server server;

private:
    std::thread thread_;
    int port_ = -1;
};

/// write-api handlers in front of either real in-process storage or an unreachable port.
class WriteApiFixture
{
public:
    explicit WriteApiFixture(bool storageUp) : client_("127.0.0.1", 1)
    {
        if (storageUp)
        {
            outpost::storage::registerStorageRoutes(storage_.server, store);
            storage_.start();
            client_ = outpost::writeApi::StorageClient("127.0.0.1", storage_.port());
        }
        outpost::writeApi::registerHandlers(api_.server, client_);
        api_.start();
    }
    httplib::Client client() const
    {
        httplib::Client cli("127.0.0.1", api_.port());
        cli.set_connection_timeout(std::chrono::seconds(2));
        cli.set_read_timeout(std::chrono::seconds(3));
        return cli;
    }
    int put(const std::string& key, const json& body) const
    {
        auto res = client().Put("/key/" + key, body.dump(), "application/json");
        return res ? res->status : -1;
    }
    outpost::storage::KvStore store;

private:
    BackgroundServer storage_;
    outpost::writeApi::StorageClient client_;
    BackgroundServer api_;
};

json validWrite(const std::string& value)
{
    return {{"value", value}, {"version", 10}, {"writerId", "edge-1"}};
}

}  // namespace

// ── Health ──────────────────────────────────────────────────────────────

TEST(WriteApiHealthTest, HealthyWhenStorageAnswers)
{
    WriteApiFixture api(true);
    auto res = api.client().Get("/health");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
}

TEST(WriteApiHealthTest, UnhealthyWhenStorageUnreachable)
{
    WriteApiFixture api(false);
    auto res = api.client().Get("/health");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 503);
}

// ── Reads ───────────────────────────────────────────────────────────────

TEST(WriteApiReadTest, MissingKeyIs404)
{
    WriteApiFixture api(true);
    auto res = api.client().Get("/key/absent");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

TEST(WriteApiReadTest, StoredKeyIsReturnedWithItsVersion)
{
    WriteApiFixture api(true);
    api.store.put("k", "v", 7, "edge-3");
    auto res = api.client().Get("/key/k");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    json body = json::parse(res->body);
    EXPECT_EQ(body["value"], "v");
    EXPECT_EQ(body["version"], 7);
    EXPECT_EQ(body["writerId"], "edge-3");
}

TEST(WriteApiReadTest, StorageFailureIs502NotNotFound)
{
    WriteApiFixture api(false);
    auto res = api.client().Get("/key/anything");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 502);
}

// ── Write validation ────────────────────────────────────────────────────

TEST(WriteApiValidationTest, KeyAt256BytesIsAcceptedAndAt257Rejected)
{
    WriteApiFixture api(true);
    EXPECT_EQ(api.put(std::string(256, 'k'), validWrite("v")), 200);
    EXPECT_EQ(api.put(std::string(257, 'k'), validWrite("v")), 400);
}

TEST(WriteApiValidationTest, ValueAtOneMebibyteIsAcceptedAndAboveRejected)
{
    WriteApiFixture api(true);
    EXPECT_EQ(api.put("at-limit", validWrite(std::string(1024 * 1024, 'v'))), 200);
    EXPECT_EQ(api.put("over-limit", validWrite(std::string(1024 * 1024 + 1, 'v'))), 400);
    EXPECT_FALSE(api.store.exists("over-limit"));
}

TEST(WriteApiValidationTest, MissingOrInvalidVersionOrWriterIsRejected)
{
    WriteApiFixture api(true);
    EXPECT_EQ(api.put("k", json({{"value", "v"}, {"writerId", "edge-1"}})), 400);
    EXPECT_EQ(api.put("k", json({{"value", "v"}, {"version", 0}, {"writerId", "edge-1"}})), 400);
    EXPECT_EQ(api.put("k", json({{"value", "v"}, {"version", -5}, {"writerId", "edge-1"}})), 400);
    EXPECT_EQ(api.put("k", json({{"value", "v"}, {"version", 1}})), 400);
    EXPECT_EQ(api.put("k", json({{"value", "v"}, {"version", 1}, {"writerId", ""}})), 400);
    EXPECT_EQ(api.put("k", json({{"value", 42}, {"version", 1}, {"writerId", "edge-1"}})), 400);
    EXPECT_FALSE(api.store.exists("k"));
}

TEST(WriteApiValidationTest, NonJsonBodyIsRejected)
{
    WriteApiFixture api(true);
    auto res = api.client().Put("/key/k", "plain text", "text/plain");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 400);
}

// ── Writes ──────────────────────────────────────────────────────────────

TEST(WriteApiWriteTest, ValidWriteIsAppliedThenDuplicateThenSuperseded)
{
    WriteApiFixture api(true);
    auto first = api.client().Put("/key/k", validWrite("v").dump(), "application/json");
    auto again = api.client().Put("/key/k", validWrite("v").dump(), "application/json");
    json older = {{"value", "old"}, {"version", 5}, {"writerId", "edge-2"}};
    auto stale = api.client().Put("/key/k", older.dump(), "application/json");
    ASSERT_TRUE(first && again && stale);
    EXPECT_EQ(json::parse(first->body)["result"], "applied");
    EXPECT_EQ(json::parse(again->body)["result"], "duplicate");
    EXPECT_EQ(json::parse(stale->body)["result"], "superseded");
    EXPECT_EQ(api.store.get("k")->value, "v");
}

TEST(WriteApiWriteTest, StorageFailureOnWriteIs502)
{
    WriteApiFixture api(false);
    EXPECT_EQ(api.put("k", validWrite("v")), 502);
}
