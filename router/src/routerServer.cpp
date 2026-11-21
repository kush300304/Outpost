/// @file routerServer.cpp
/// @brief Router implementation: ring lookup, request forwarding and health-driven membership.

#include "router/routerServer.h"

#include <spdlog/spdlog.h>

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

#include "common/urlEncode.h"

using json = nlohmann::json;

namespace outpost
{
namespace router
{

RouterServer::RouterServer(const RouterConfig& config) : config_(config), ring_(config.virtualNodes)
{
    if (config_.edges.empty() || config_.unhealthyThreshold < 1)
    {
        throw std::invalid_argument(
            "RouterConfig: needs at least one edge and unhealthyThreshold >= 1");
    }
    for (const EdgeNode& node : config_.edges)
    {
        auto status = std::make_unique<EdgeStatus>();
        status->node = node;
        if (!edges_.emplace(node.id, std::move(status)).second)
        {
            throw std::invalid_argument("RouterConfig: duplicate edge id " + node.id);
        }
        ring_.addNode(node.id);
    }

    server_.Get("/health", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        json body = {{"status", "healthy"},
                     {"service", "router"},
                     {"edgesInRing", ring_.nodeCount()},
                     {"edgesConfigured", edges_.size()}};
        res.set_content(body.dump(), "application/json");
    });

    server_.Get(R"(/key/(.+))",
                [this](const httplib::Request& req, httplib::Response& res) { forward(req, res); });
    server_.Put(R"(/key/(.+))",
                [this](const httplib::Request& req, httplib::Response& res) { forward(req, res); });

    // Purge goes to every configured edge, not only those on the ring: an edge that is off the
    // ring may still hold the key and serve it once it rejoins.
    server_.Post(R"(/purge/key/(.+))", [this](const httplib::Request& req, httplib::Response& res) {
        std::string key = req.matches[1].str();
        purges_.fetch_add(1, std::memory_order_relaxed);
        json results = json::array();
        bool complete = true;
        for (const auto& [id, edge] : edges_)
        {
            httplib::Client cli(edge->node.host, edge->node.port);
            cli.set_address_family(AF_INET);
            cli.set_url_encode(false);
            cli.set_connection_timeout(config_.edgeTimeout);
            cli.set_read_timeout(config_.edgeTimeout);
            auto result =
                cli.Post("/purge/key/" + common::encodePathSegment(key), "", "text/plain");
            bool reached = result && result->status == 200;
            json answer = reached ? json::parse(result->body, nullptr, false) : json();
            bool purged = answer.is_object() && answer.value("purged", false);
            complete = complete && reached;
            results.push_back({{"nodeId", id}, {"reached", reached}, {"purged", purged}});
        }
        res.status = complete ? 200 : 502;
        res.set_content(json({{"key", key}, {"complete", complete}, {"edges", results}}).dump(),
                        "application/json");
    });

    server_.Get(R"(/debug/route/(.+))",
                [this](const httplib::Request& req, httplib::Response& res) {
                    std::string key = req.matches[1].str();
                    json body = {{"key", key}, {"node", ring_.getNode(key)}};
                    res.set_content(body.dump(), "application/json");
                });

    server_.Get("/debug/stats", [this](const httplib::Request& /*req*/, httplib::Response& res) {
        json edges = json::array();
        for (const auto& [id, edge] : edges_)
        {
            edges.push_back({{"id", id},
                             {"address", edge->node.host + ":" + std::to_string(edge->node.port)},
                             {"inRing", ring_.hasNode(id)},
                             {"consecutiveFailures", edge->consecutiveFailures.load()},
                             {"routed", edge->routed.load()}});
        }
        json body = {{"service", "router"},
                     {"requests", requests_.load()},
                     {"failovers", failovers_.load()},
                     {"purges", purges_.load()},
                     {"virtualNodes", ring_.virtualNodeCount()},
                     {"edges", edges}};
        res.set_content(body.dump(2), "application/json");
    });

    spdlog::info("Router initialized with {} edges, {} ring points", edges_.size(),
                 ring_.virtualNodeCount());
}

RouterServer::~RouterServer()
{
    stop();
}

int RouterServer::bind()
{
    int port = config_.port == 0
                   ? server_.bind_to_any_port(config_.bindAddress)
                   : (server_.bind_to_port(config_.bindAddress, config_.port) ? config_.port : -1);
    if (port < 0)
    {
        spdlog::error("Router failed to bind {}:{}", config_.bindAddress, config_.port);
    }
    return port;
}

bool RouterServer::serve()
{
    {
        std::lock_guard<std::mutex> lock(stopMutex_);
        stopping_ = false;
    }
    healthThread_ = std::thread(&RouterServer::healthLoop, this);
    bool served = server_.listen_after_bind();
    stop();
    return served;
}

void RouterServer::stop()
{
    {
        std::lock_guard<std::mutex> lock(stopMutex_);
        stopping_ = true;
    }
    stopSignal_.notify_all();
    server_.stop();
    std::lock_guard<std::mutex> joinLock(joinMutex_);
    if (healthThread_.joinable())
    {
        healthThread_.join();
    }
}

void RouterServer::forward(const httplib::Request& req, httplib::Response& res)
{
    requests_.fetch_add(1, std::memory_order_relaxed);
    std::string key = req.matches[1].str();
    std::string path = "/key/" + common::encodePathSegment(key);
    std::string forwardedFor = req.get_header_value("X-Forwarded-For");
    forwardedFor = forwardedFor.empty() ? req.remote_addr : forwardedFor + ", " + req.remote_addr;
    httplib::Headers headers = {{"X-Forwarded-For", forwardedFor}};
    for (const char* name : {"If-None-Match", "Cache-Control"})
    {
        if (req.has_header(name))
        {
            headers.emplace(name, req.get_header_value(name));
        }
    }
    std::string contentType = req.get_header_value("Content-Type");
    contentType = contentType.empty() ? "text/plain" : contentType;

    bool delivered = false;
    bool retry = true;
    int attempts = 0;
    std::string failure = "no edge is on the ring";
    while (!delivered && retry && attempts < 2)
    {
        std::string owner = ring_.getNode(key);
        retry = false;
        if (!owner.empty())
        {
            EdgeStatus& edge = *edges_.at(owner);
            httplib::Client cli(edge.node.host, edge.node.port);
            cli.set_address_family(AF_INET);
            cli.set_url_encode(false);
            cli.set_connection_timeout(config_.edgeTimeout);
            cli.set_read_timeout(config_.edgeTimeout);
            auto result = req.method == "PUT" ? cli.Put(path, headers, req.body, contentType)
                                              : cli.Get(path, headers);
            ++attempts;
            if (result)
            {
                delivered = true;
                edge.routed.fetch_add(1, std::memory_order_relaxed);
                res.status = result->status;
                for (const char* name : {"X-Cache", "X-Node", "X-Write-Mode", "Retry-After", "ETag",
                                         "Last-Modified", "Age", "Cache-Control"})
                {
                    if (result->has_header(name))
                    {
                        res.set_header(name, result->get_header_value(name));
                    }
                }
                std::string type = result->get_header_value("Content-Type");
                if (result->status != 304)
                {
                    res.set_content(result->body, type.empty() ? "application/json" : type);
                }
            }
            else if (result.error() == httplib::Error::Connection)
            {
                edge.consecutiveFailures.store(config_.unhealthyThreshold);
                retry = ring_.removeNode(owner);
                failovers_.fetch_add(1, std::memory_order_relaxed);
                failure = owner + " refused the connection";
                spdlog::warn("Edge {} refused a connection: removed from the ring, retrying",
                             owner);
            }
            else
            {
                failure = owner + ": " + httplib::to_string(result.error());
            }
        }
    }
    if (!delivered)
    {
        res.status = 503;
        res.set_content(
            json({{"error", "edge_unavailable"}, {"message", failure}, {"key", key}}).dump(),
            "application/json");
    }
}

void RouterServer::healthLoop()
{
    std::unique_lock<std::mutex> lock(stopMutex_);
    while (!stopSignal_.wait_for(lock, config_.healthCheckInterval, [this] { return stopping_; }))
    {
        lock.unlock();
        for (const auto& [id, edge] : edges_)
        {
            httplib::Client cli(edge->node.host, edge->node.port);
            cli.set_address_family(AF_INET);
            cli.set_connection_timeout(config_.edgeTimeout);
            cli.set_read_timeout(config_.edgeTimeout);
            auto result = cli.Get("/health");
            bool healthy = result && result->status == 200;
            if (healthy)
            {
                edge->consecutiveFailures.store(0);
                if (ring_.addNode(id))
                {
                    spdlog::info("Edge {} is healthy again: added back to the ring", id);
                }
            }
            else
            {
                int failures = edge->consecutiveFailures.fetch_add(1) + 1;
                if (failures >= config_.unhealthyThreshold && ring_.removeNode(id))
                {
                    spdlog::warn("Edge {} failed {} health checks: removed from the ring", id,
                                 failures);
                }
            }
        }
        lock.lock();
    }
}

}  // namespace router
}  // namespace outpost
