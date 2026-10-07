#include <cstdlib>
#include <iostream>
#include <string>
#include <httplib.h>

int main() {
    int exitCode = 0;
    const char* envPort = std::getenv("PORT");
    int port = 8090;
    if (envPort != nullptr) {
        port = std::atoi(envPort);
    }

    httplib::Server server;

    server.Get("/health", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.status = 200;
        res.set_content("{\"status\":\"healthy\",\"service\":\"write-api\"}", "application/json");
    });

    std::cout << "Starting Outpost Write-API service on port " << port << std::endl;
    bool success = server.listen("0.0.0.0", port);
    if (!success) {
        std::cerr << "Failed to start Write-API service on port " << port << std::endl;
        exitCode = 1;
    }

    return exitCode;
}
