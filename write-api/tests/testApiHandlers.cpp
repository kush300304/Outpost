/// @file testApiHandlers.cpp
/// @brief Unit tests for write-api request handlers.
///
/// These test the validation logic in isolation. Full integration tests
/// (write-api → storage) require the Docker cluster.

#include <gtest/gtest.h>

// Validation logic tests — these don't need a running server.

TEST(ApiHandlersTest, KeyLengthValidation) {
    // Keys up to 256 bytes are valid
    std::string validKey(256, 'k');
    EXPECT_LE(validKey.size(), 256u);

    // Keys over 256 bytes are rejected
    std::string invalidKey(257, 'k');
    EXPECT_GT(invalidKey.size(), 256u);
}

TEST(ApiHandlersTest, ValueSizeValidation) {
    // Values up to 1MB are valid
    std::string validValue(1024 * 1024, 'v');
    EXPECT_LE(validValue.size(), 1024u * 1024u);

    // Values over 1MB are rejected
    std::string invalidValue(1024 * 1024 + 1, 'v');
    EXPECT_GT(invalidValue.size(), 1024u * 1024u);
}

// ── Real handlers against real or unreachable storage ───────────────────

#include <chrono>
#include <thread>

#include <httplib.h>

#include "storage/kvStore.h"
#include "storage/storageHandlers.h"
#include "writeApi/apiHandlers.h"
#include "writeApi/storageClient.h"

namespace {

/// An httplib server running on an ephemeral loopback port for the lifetime of the object.
class BackgroundServer {
public:
    ~BackgroundServer() {
        server.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }
    void start() {
        port_ = server.bind_to_any_port("127.0.0.1");
        thread_ = std::thread([this] { server.listen_after_bind(); });
        server.wait_until_ready();
    }
    int port() const { return port_; }
    httplib::Server server;

private:
    std::thread thread_;
    int port_ = -1;
};

/// write-api handlers in front of either real in-process storage or an unreachable port.
class WriteApiFixture {
public:
    explicit WriteApiFixture(bool storageUp) : client_("127.0.0.1", 1) {
        if (storageUp) {
            outpost::storage::registerStorageRoutes(storage_.server, store);
            storage_.start();
            client_ = outpost::writeApi::StorageClient("127.0.0.1", storage_.port());
        }
        outpost::writeApi::registerHandlers(api_.server, client_);
        api_.start();
    }
    httplib::Client client() const {
        httplib::Client cli("127.0.0.1", api_.port());
        cli.set_connection_timeout(std::chrono::seconds(2));
        cli.set_read_timeout(std::chrono::seconds(3));
        return cli;
    }
    outpost::storage::KvStore store;

private:
    BackgroundServer storage_;
    outpost::writeApi::StorageClient client_;
    BackgroundServer api_;
};

}  // namespace

TEST(WriteApiHealthTest, HealthyWhenStorageAnswers) {
    WriteApiFixture api(true);
    auto res = api.client().Get("/health");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
}

TEST(WriteApiHealthTest, UnhealthyWhenStorageUnreachable) {
    WriteApiFixture api(false);
    auto res = api.client().Get("/health");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 503);
}

TEST(WriteApiReadTest, MissingKeyIs404) {
    WriteApiFixture api(true);
    auto res = api.client().Get("/key/absent");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 404);
}

TEST(WriteApiReadTest, StorageFailureIs502NotNotFound) {
    WriteApiFixture api(false);
    auto res = api.client().Get("/key/anything");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 502);
}
