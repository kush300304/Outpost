#pragma once

/// @file apiHandlers.h
/// @brief HTTP route handlers for the write-api service.
///
/// Registers GET/PUT/DELETE /key/:id routes that validate requests and
/// proxy them to the storage service via StorageClient. Also provides
/// /health and /debug/stats diagnostic endpoints.

#include <httplib.h>

#include "writeApi/storageClient.h"

namespace outpost {
namespace writeApi {

/// @brief Register all HTTP routes on the given server.
///
/// Routes:
///   GET    /health       → 200 health check
///   GET    /key/:id      → proxy GET to storage
///   PUT    /key/:id      → validate, then proxy PUT to storage
///   DELETE /key/:id      → proxy DELETE to storage
///   GET    /debug/stats  → service diagnostics
///
/// @param server  The httplib server to register routes on.
/// @param client  The storage client to use for proxying requests.
void registerHandlers(httplib::Server& server, StorageClient& client);

}  // namespace writeApi
}  // namespace outpost
