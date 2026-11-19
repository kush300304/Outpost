/// @file testEdgeServer.cpp
/// @brief In-process tests for outpost::edge::EdgeServer against fake and real origins.
///
/// FakeOrigin is a scriptable stand-in for write-api. The "real chain" tests run the actual
/// write-api handlers and storage handlers in-process on ephemeral ports.

#include "edge/edgeServer.h"

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

#include "common/wal.h"
#include "storage/kvStore.h"
#include "storage/storageHandlers.h"
#include "writeApi/apiHandlers.h"
#include "writeApi/storageClient.h"

using json = nlohmann::json;
using outpost::common::WalEntry;
using outpost::common::WalOperation;
using outpost::common::WriteAheadLog;
using outpost::edge::EdgeConfig;
using outpost::edge::EdgeServer;
using namespace std::chrono_literals;

namespace fs = std::filesystem;

namespace {

/// Polls predicate until it holds or the timeout expires; returns the last result.
bool waitUntil(const std::function<bool()>& predicate, std::chrono::milliseconds timeout = 3000ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    bool satisfied = predicate();
    while (!satisfied && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
        satisfied = predicate();
    }
    return satisfied;
}

/// An httplib server running on an ephemeral loopback port for the lifetime of the object.
class BackgroundServer {
public:
    BackgroundServer() = default;
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

/// Scriptable write-api stand-in: records PUTs and answers with putStatus(key, attempt).
class FakeOrigin {
public:
    struct Put {
        std::string key;
        json body;
        int status;
    };

    FakeOrigin() {
        http_.server.Get("/health", [this](const httplib::Request&, httplib::Response& res) {
            ++healthChecks;
            std::this_thread::sleep_for(std::chrono::milliseconds(healthDelayMs.load()));
            res.status = healthy ? 200 : 503;
        });
        http_.server.Get(R"(/key/(.+))", [this](const httplib::Request& req,
                                                httplib::Response& res) {
            ++keyGets;
            std::string key = req.matches[1].str();
            res.status = getStatus ? getStatus(key) : 404;
            json body = res.status == 200 ? json({{"key", key}, {"value", "origin-" + key}})
                                          : json({{"error", "not_found"}});
            res.set_content(body.dump(), "application/json");
        });
        http_.server.Put(R"(/key/(.+))", [this](const httplib::Request& req,
                                                httplib::Response& res) {
            std::string key = req.matches[1].str();
            int attempt = 0;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const Put& put : puts_) {
                    attempt += put.key == key ? 1 : 0;
                }
            }
            if (onPut) {
                onPut(key);
            }
            int status = putStatus ? putStatus(key, attempt) : 200;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                puts_.push_back(Put{key, json::parse(req.body, nullptr, false), status});
            }
            res.status = status;
            res.set_content(json({{"key", key}, {"result", "applied"}}).dump(), "application/json");
        });
        http_.start();
    }

    int port() const { return http_.port(); }

    std::vector<Put> puts() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return puts_;
    }

    bool received(const std::string& key, int status) const {
        bool found = false;
        for (const Put& put : puts()) {
            found = found || (put.key == key && put.status == status);
        }
        return found;
    }

    std::atomic<bool> healthy{true};
    std::atomic<int> healthChecks{0};
    std::atomic<int> healthDelayMs{0};
    std::atomic<int> keyGets{0};
    std::function<int(const std::string& key)> getStatus;
    std::function<int(const std::string& key, int attempt)> putStatus;
    std::function<void(const std::string& key)> onPut;

private:
    mutable std::mutex mutex_;
    std::vector<Put> puts_;
    BackgroundServer http_;
};

/// Real write-api handlers in front of real storage handlers, both in-process.
class RealOrigin {
public:
    RealOrigin() : storageClient_("127.0.0.1", 0) {
        outpost::storage::registerStorageRoutes(storageHttp_.server, store);
        storageHttp_.start();
        storageClient_ = outpost::writeApi::StorageClient("127.0.0.1", storageHttp_.port());
        outpost::writeApi::registerHandlers(writeApiHttp_.server, storageClient_);
        writeApiHttp_.start();
    }
    int port() const { return writeApiHttp_.port(); }
    outpost::storage::KvStore store;

private:
    BackgroundServer storageHttp_;
    outpost::writeApi::StorageClient storageClient_;
    BackgroundServer writeApiHttp_;
};

/// An EdgeServer serving on an ephemeral port in a background thread.
class RunningEdge {
public:
    explicit RunningEdge(const EdgeConfig& config) : server_(config) {
        port_ = server_.bind();
        thread_ = std::thread([this] { server_.serve(); });
        waitUntil([this] { return client().Get("/health") != nullptr; });
    }
    ~RunningEdge() {
        server_.stop();
        thread_.join();
    }
    httplib::Client client() const {
        httplib::Client cli("127.0.0.1", port_);
        cli.set_connection_timeout(1s);
        cli.set_read_timeout(3s);
        return cli;
    }
    json stats() const {
        auto res = client().Get("/debug/stats");
        return res ? json::parse(res->body, nullptr, false) : json();
    }
    std::string breakerState() const {
        json body = stats();
        return body.is_object() ? body["circuitBreaker"]["state"].get<std::string>() : "";
    }
    std::size_t pending() const {
        json body = stats();
        return body.is_object() ? body["writeQueue"]["pending"].get<std::size_t>() : 9999;
    }

private:
    EdgeServer server_;
    std::thread thread_;
    int port_ = -1;
};

/// Unique queue WAL path per test, removed afterwards.
class EdgeServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        walPath_ = (fs::temp_directory_path() /
                    ("outpost_edge_" + std::to_string(::getpid()) + "_" +
                     ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".wal"))
                       .string();
        fs::remove(walPath_);
    }
    void TearDown() override {
        std::error_code ec;
        fs::remove(walPath_, ec);
    }
    EdgeConfig config(int originPort) const {
        EdgeConfig cfg;
        cfg.nodeId = "edge-1";
        cfg.bindAddress = "127.0.0.1";
        cfg.port = 0;
        cfg.writeApiHost = "127.0.0.1";
        cfg.writeApiPort = originPort;
        cfg.breakerFailureThreshold = 2;
        cfg.breakerResetTimeout = 200ms;
        cfg.healthCheckInterval = 50ms;
        cfg.originTimeout = 1000ms;
        cfg.walPath = walPath_;
        cfg.walFsync = false;
        return cfg;
    }
    void queueOnDisk(const std::vector<WalEntry>& entries) const {
        WriteAheadLog wal(walPath_, false);
        for (const WalEntry& entry : entries) {
            wal.append(entry);
        }
    }
    std::string walPath_;
};

}  // namespace

// ── Phase A: queue drain ────────────────────────────────────────────────

TEST_F(EdgeServerTest, WriteQueuedWhileBreakerStaysClosedIsDrained) {
    FakeOrigin origin;
    origin.putStatus = [](const std::string&, int attempt) { return attempt == 0 ? 503 : 200; };
    RunningEdge edge(config(origin.port()));

    auto res = edge.client().Put("/key/k", R"({"value":"v"})", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 202);
    EXPECT_EQ(res->get_header_value("X-Write-Mode"), "QUEUED");

    EXPECT_TRUE(waitUntil([&] { return origin.received("k", 200); }));
    EXPECT_TRUE(waitUntil([&] { return edge.pending() == 0; }));
}

TEST_F(EdgeServerTest, RestartedEdgeDrainsLeftoverQueue) {
    queueOnDisk({WalEntry{WalOperation::PUT, "a", "1", 10, "edge-1"},
                 WalEntry{WalOperation::PUT, "b", "2", 11, "edge-1"}});
    FakeOrigin origin;
    RunningEdge edge(config(origin.port()));

    EXPECT_TRUE(waitUntil([&] { return origin.received("a", 200) && origin.received("b", 200); }));
    EXPECT_TRUE(waitUntil([&] { return edge.pending() == 0; }));
    auto puts = origin.puts();
    ASSERT_GE(puts.size(), 2u);
    EXPECT_EQ(puts[0].body["version"], 10);
    EXPECT_EQ(puts[0].body["writerId"], "edge-1");
}

TEST_F(EdgeServerTest, WriteQueuedDuringDrainIsNotLost) {
    queueOnDisk({WalEntry{WalOperation::PUT, "slow", "1", 10, "edge-1"}});
    FakeOrigin origin;
    std::atomic<bool> drainStarted{false};
    origin.onPut = [&](const std::string& key) {
        if (key == "slow") {
            drainStarted = true;
            std::this_thread::sleep_for(300ms);
        }
    };
    origin.putStatus = [](const std::string& key, int attempt) {
        return key == "late" && attempt == 0 ? 503 : 200;
    };
    RunningEdge edge(config(origin.port()));

    ASSERT_TRUE(waitUntil([&] { return drainStarted.load(); }));
    auto res = edge.client().Put("/key/late", R"({"value":"x"})", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 202);

    EXPECT_TRUE(waitUntil([&] { return origin.received("late", 200); }));
    EXPECT_TRUE(waitUntil([&] { return edge.pending() == 0; }));
}

TEST_F(EdgeServerTest, OlderQueuedWriteNeverOverwritesNewerStoredValue) {
    RealOrigin origin;
    origin.store.put("k", "newer", 2000, "edge-2");
    queueOnDisk({WalEntry{WalOperation::PUT, "k", "older", 1000, "edge-1"}});
    RunningEdge edge(config(origin.port()));

    EXPECT_TRUE(waitUntil([&] { return origin.store.counters().superseded == 1; }));
    EXPECT_TRUE(waitUntil([&] { return edge.pending() == 0; }));
    EXPECT_EQ(origin.store.get("k")->value, "newer");
}

TEST_F(EdgeServerTest, ReplayAfterCrashBeforeDiscardIsIdempotent) {
    RealOrigin origin;
    WalEntry write{WalOperation::PUT, "k", "v", 1000, "edge-1"};
    queueOnDisk({write});
    {
        RunningEdge edge(config(origin.port()));
        ASSERT_TRUE(waitUntil([&] { return edge.pending() == 0; }));
    }
    queueOnDisk({write});  // the same write is still queued, as if the edge died before discarding
    RunningEdge edge(config(origin.port()));

    EXPECT_TRUE(waitUntil([&] { return origin.store.counters().duplicates == 1; }));
    EXPECT_EQ(origin.store.counters().applied, 1u);
    EXPECT_EQ(origin.store.get("k")->value, "v");
}

// ── Phase B: failure detection ──────────────────────────────────────────

TEST_F(EdgeServerTest, BreakerOpensAfterThresholdFailedHealthChecks) {
    FakeOrigin origin;
    origin.healthy = false;
    RunningEdge edge(config(origin.port()));
    EXPECT_TRUE(waitUntil([&] { return edge.breakerState() == "OPEN"; }, 1000ms));
}

TEST_F(EdgeServerTest, OpenBreakerSendsNoOriginTrafficUntilResetTimeout) {
    FakeOrigin origin;
    origin.healthy = false;
    EdgeConfig cfg = config(origin.port());
    cfg.breakerResetTimeout = 600ms;
    RunningEdge edge(cfg);
    ASSERT_TRUE(waitUntil([&] { return edge.breakerState() == "OPEN"; }, 1000ms));

    int healthChecksAtOpen = origin.healthChecks.load();
    auto res = edge.client().Get("/key/uncached");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 503);
    std::this_thread::sleep_for(300ms);
    EXPECT_EQ(origin.healthChecks.load(), healthChecksAtOpen);
    EXPECT_EQ(origin.keyGets.load(), 0);
}

TEST_F(EdgeServerTest, HalfOpenAdmitsExactlyOneProbeBeforeClosing) {
    FakeOrigin origin;
    origin.healthy = false;
    EdgeConfig cfg = config(origin.port());
    cfg.breakerResetTimeout = 200ms;
    RunningEdge edge(cfg);
    ASSERT_TRUE(waitUntil([&] { return edge.breakerState() == "OPEN"; }, 1000ms));
    auto openedAt = std::chrono::steady_clock::now();
    int healthChecksAtOpen = origin.healthChecks.load();
    origin.healthDelayMs = 300;
    origin.healthy = true;

    ASSERT_TRUE(waitUntil([&] { return edge.breakerState() == "HALF_OPEN"; }, 1000ms));
    for (int i = 0; i < 5; ++i) {
        auto res = edge.client().Get("/key/during-probe-" + std::to_string(i));
        ASSERT_TRUE(res);
        EXPECT_EQ(res->status, 503);
    }
    ASSERT_TRUE(waitUntil([&] { return edge.breakerState() == "CLOSED"; }, 2000ms));
    EXPECT_GE(std::chrono::steady_clock::now() - openedAt, 200ms);
    EXPECT_EQ(origin.healthChecks.load(), healthChecksAtOpen + 1);
    EXPECT_EQ(origin.keyGets.load(), 0);
}

TEST_F(EdgeServerTest, CachedReadWhileBreakerOpenIsStale) {
    FakeOrigin origin;
    RunningEdge edge(config(origin.port()));
    auto put = edge.client().Put("/key/k", R"({"value":"cached"})", "application/json");
    ASSERT_TRUE(put);
    ASSERT_EQ(put->status, 200);

    origin.healthy = false;
    ASSERT_TRUE(waitUntil([&] { return edge.breakerState() == "OPEN"; }, 1000ms));
    auto res = edge.client().Get("/key/k");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    EXPECT_EQ(res->get_header_value("X-Cache"), "STALE");
    EXPECT_EQ(json::parse(res->body)["value"], "cached");
}

TEST_F(EdgeServerTest, UncachedReadWhenOriginFailsIs503NotNotFound) {
    FakeOrigin origin;
    origin.getStatus = [](const std::string&) { return 502; };
    RunningEdge edge(config(origin.port()));
    auto res = edge.client().Get("/key/k");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 503);
    EXPECT_EQ(res->get_header_value("X-Cache"), "MISS");
}

TEST_F(EdgeServerTest, MissingKeyIs404AndFoundKeyIsMissThenHit) {
    FakeOrigin origin;
    origin.getStatus = [](const std::string& key) { return key == "present" ? 200 : 404; };
    RunningEdge edge(config(origin.port()));
    auto absent = edge.client().Get("/key/absent");
    ASSERT_TRUE(absent);
    EXPECT_EQ(absent->status, 404);
    auto first = edge.client().Get("/key/present");
    auto second = edge.client().Get("/key/present");
    ASSERT_TRUE(first && second);
    EXPECT_EQ(first->get_header_value("X-Cache"), "MISS");
    EXPECT_EQ(second->get_header_value("X-Cache"), "HIT");
    EXPECT_EQ(origin.keyGets.load(), 2);
}
