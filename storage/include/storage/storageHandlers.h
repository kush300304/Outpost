#pragma once

/// @file storageHandlers.h
/// @brief HTTP routes of the storage service.
///
/// Routes:
///   GET /health       → 200 {"status":"healthy","service":"storage"}
///   GET /key/:id      → 200 {"key","value","version","writerId"} or 404
///   PUT /key/:id      → body {"value":string,"version":uint>0,"writerId":string}
///                       → 200 {"key","result":"applied|duplicate|superseded","version","writerId"}
///                         where version/writerId describe the value now stored; 400 on a bad body
///   GET /debug/stats  → key count, put outcome counters, WAL recovery report
///   GET /debug/keys   → {"keys":[...]}
///
/// Only write-api talks to this service; it is not reachable from outside the compose network.

#include <httplib.h>

#include "storage/kvStore.h"

namespace outpost
{
namespace storage
{

/// @brief Register all storage routes on server, backed by store.
void registerStorageRoutes(httplib::Server& server, KvStore& store);

}  // namespace storage
}  // namespace outpost
