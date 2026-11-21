#pragma once

/// @file httpCache.h
/// @brief Entity tags for standard HTTP caching (RFC 9110 §8.8.3, §13.1.2).
///
/// A stored value is identified by the write that produced it, so its entity tag is the quoted
/// pair "<version>-<writerId>": two copies with the same tag are the same write. Storage and the
/// edges use these helpers to answer If-None-Match with 304 Not Modified.

#include <cstdint>
#include <string>

namespace outpost
{
namespace common
{

/// @brief The strong entity tag of a value written with (version, writerId).
std::string makeEtag(uint64_t version, const std::string& writerId);

/// @brief Whether an If-None-Match header value matches etag.
///
/// Accepts a comma-separated list, "*" (matches any existing value) and weak tags (W/"..."),
/// compared with the weak comparison that If-None-Match requires. An empty etag (no stored value)
/// never matches.
bool etagMatches(const std::string& ifNoneMatch, const std::string& etag);

}  // namespace common
}  // namespace outpost
