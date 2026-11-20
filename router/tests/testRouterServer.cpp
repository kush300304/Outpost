/// @file testRouterServer.cpp
/// @brief In-process tests for outpost::router::RouterServer against fake edges.
///
/// Each FakeEdge is an httplib server on an ephemeral port that answers /key requests with its
/// own id in X-Node and records the decoded key, body and X-Forwarded-For it received.

#include "router/routerServer.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>
#include <httplib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using outpost::router::EdgeNode;
using outpost::router::RouterConfig;
using outpost::router::RouterServer;
using namespace std::chrono_literals;

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

/// A stand-in edge node; can be stopped and restarted on the same port.
class FakeEdge {
public:
    explicit FakeEdge(std::string id) : id_(std::move(id)) {
        start(0);
    }
    ~FakeEdge() {
        stop();
    }
    void start(int port) {
        server_ = std::make_unique<httplib::Server>();
        server_->Get("/health", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"status":"healthy"})", "application/json");
        });
        auto handler = [this](const httplib::Request& req, httplib::Response& res) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                lastKey = req.matches[1].str();
                lastBody = req.body;
                lastForwardedFor = req.get_header_value("X-Forwarded-For");
            }
            ++requests;
            res.status = replyStatus.load();
            res.set_header("X-Node", id_);
            res.set_header("X-Cache", "HIT");
            res.set_header("X-Write-Mode", "DIRECT");
            res.set_header("Retry-After", "1");
            res.set_content(json({{"key", req.matches[1].str()}, {"servedBy", id_}}).dump(),
                            "application/json");
        };
        server_->Get(R"(/key/(.+))", handler);
        server_->Put(R"(/key/(.+))", handler);
        port_ = port == 0 ? server_->bind_to_any_port("127.0.0.1")
                          : (server_->bind_to_port("127.0.0.1", port) ? port : -1);
        thread_ = std::thread([this] { server_->listen_after_bind(); });
        server_->wait_until_ready();
    }
    void stop() {
        if (server_) {
            server_->stop();
            thread_.join();
            server_.reset();
        }
    }
    EdgeNode node() const { return EdgeNode{id_, "127.0.0.1", port_}; }
    int port() const { return port_; }
    std::string lastSeenKey() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastKey;
    }
    std::string lastSeenForwardedFor() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastForwardedFor;
    }
    std::string lastSeenBody() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastBody;
    }
    std::atomic<int> requests{0};
    std::atomic<int> replyStatus{200};

private:
    std::string id_;
    std::unique_ptr<httplib::Server> server_;
    std::thread thread_;
    int port_ = -1;
    mutable std::mutex mutex_;
    std::string lastKey;
    std::string lastBody;
    std::string lastForwardedFor;
};

/// A RouterServer in front of three fake edges, serving on an ephemeral port.
class RouterFixture : public ::testing::Test {
protected:
    void SetUp() override {
        for (const char* id : {"edge-1", "edge-2", "edge-3"}) {
            edges_.push_back(std::make_unique<FakeEdge>(id));
        }
        RouterConfig config;
        config.bindAddress = "127.0.0.1";
        config.port = 0;
        for (const auto& edge : edges_) {
            config.edges.push_back(edge->node());
        }
        config.healthCheckInterval = 50ms;
        config.edgeTimeout = 500ms;
        config.unhealthyThreshold = 2;
        router_ = std::make_unique<RouterServer>(config);
        port_ = router_->bind();
        thread_ = std::thread([this] { router_->serve(); });
        ASSERT_TRUE(waitUntil([this] { return client().Get("/health") != nullptr; }));
    }
    void TearDown() override {
        router_->stop();
        thread_.join();
    }
    httplib::Client client() const {
        httplib::Client cli("127.0.0.1", port_);
        cli.set_connection_timeout(1s);
        cli.set_read_timeout(3s);
        return cli;
    }
    std::string servedBy(const std::string& key) const {
        auto res = client().Get("/key/" + key);
        return res && res->status == 200 ? res->get_header_value("X-Node") : "";
    }
    std::string routeOf(const std::string& key) const {
        auto res = client().Get("/debug/route/" + key);
        return res ? json::parse(res->body)["node"].get<std::string>() : "";
    }
    bool inRing(const std::string& id) const {
        auto res = client().Get("/debug/stats");
        bool found = false;
        for (const json& edge : json::parse(res ? res->body : "{}").value("edges", json::array())) {
            found = found || (edge["id"] == id && edge["inRing"] == true);
        }
        return found;
    }
    std::vector<std::unique_ptr<FakeEdge>> edges_;
    std::unique_ptr<RouterServer> router_;
    std::thread thread_;
    int port_ = -1;
};

}  // namespace

TEST_F(RouterFixture, SameKeyAlwaysReachesTheSameEdgeNamedByDebugRoute) {
    for (int i = 0; i < 50; ++i) {
        std::string key = "key_" + std::to_string(i);
        std::string first = servedBy(key);
        ASSERT_FALSE(first.empty()) << key;
        EXPECT_EQ(servedBy(key), first) << key;
        EXPECT_EQ(routeOf(key), first) << key;
    }
}

TEST_F(RouterFixture, KeysSpreadAcrossAllEdges) {
    std::map<std::string, int> counts;
    for (int i = 0; i < 300; ++i) {
        ++counts[servedBy("spread_" + std::to_string(i))];
    }
    ASSERT_EQ(counts.size(), 3u);
    for (const auto& [edge, count] : counts) {
        EXPECT_GT(count, 45) << edge;
    }
}

TEST_F(RouterFixture, StoppingAnEdgeMovesOnlyItsKeysAndRequestsKeepSucceeding) {
    std::map<std::string, std::string> before;
    for (int i = 0; i < 200; ++i) {
        std::string key = "move_" + std::to_string(i);
        before[key] = servedBy(key);
    }
    edges_[1]->stop();  // edge-2

    for (const auto& [key, owner] : before) {
        std::string after = servedBy(key);
        ASSERT_FALSE(after.empty()) << key << " failed after edge-2 stopped";
        if (owner != "edge-2") {
            EXPECT_EQ(after, owner) << key << " moved although its edge is up";
        } else {
            EXPECT_NE(after, "edge-2") << key;
        }
    }
    EXPECT_FALSE(inRing("edge-2"));
}

TEST_F(RouterFixture, RecoveredEdgeRejoinsTheRingAndGetsItsKeysBack) {
    std::string key;
    for (int i = 0; key.empty(); ++i) {
        std::string candidate = "rejoin_" + std::to_string(i);
        key = routeOf(candidate) == "edge-2" ? candidate : "";
    }
    int port = edges_[1]->port();
    edges_[1]->stop();
    ASSERT_TRUE(waitUntil([&] { return !inRing("edge-2"); }));
    EXPECT_NE(servedBy(key), "edge-2");

    edges_[1]->start(port);
    ASSERT_TRUE(waitUntil([&] { return inRing("edge-2"); }));
    EXPECT_EQ(servedBy(key), "edge-2");
}

TEST_F(RouterFixture, ForwardsDecodedKeyClientAddressAndBody) {
    auto res = client().Put("/key/a%3Fb%20c%2Fd", {{"X-Forwarded-For", "203.0.113.7"}},
                            R"({"value":"v"})", "application/json");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 200);
    FakeEdge* served = nullptr;
    for (const auto& edge : edges_) {
        served = edge->requests.load() > 0 ? edge.get() : served;
    }
    ASSERT_NE(served, nullptr);
    EXPECT_EQ(served->lastSeenKey(), "a?b c/d");
    EXPECT_EQ(served->lastSeenBody(), R"({"value":"v"})");
    EXPECT_EQ(served->lastSeenForwardedFor().rfind("203.0.113.7", 0), 0u)
        << served->lastSeenForwardedFor();
}

TEST_F(RouterFixture, EdgeStatusAndHeadersPassThrough) {
    for (const auto& edge : edges_) {
        edge->replyStatus = 429;
    }
    auto res = client().Get("/key/anything");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 429);
    EXPECT_EQ(res->get_header_value("Retry-After"), "1");
    EXPECT_EQ(res->get_header_value("X-Cache"), "HIT");
    EXPECT_EQ(res->get_header_value("X-Write-Mode"), "DIRECT");
    EXPECT_FALSE(res->get_header_value("X-Node").empty());
}

TEST_F(RouterFixture, NoHealthyEdgeIs503) {
    for (const auto& edge : edges_) {
        edge->stop();
    }
    auto res = client().Get("/key/k");
    ASSERT_TRUE(res);
    EXPECT_EQ(res->status, 503);
}
