#pragma once

/// @file urlEncode.h
/// @brief Percent-encoding of a single URL path segment (RFC 3986).
///
/// Keys travel between services as the last segment of /key/<segment>. Every byte outside the
/// RFC 3986 unreserved set (ALPHA, DIGIT, "-", ".", "_", "~") is encoded as %XX, so keys
/// containing "/", "?", "#", "%", "+" or spaces reach the next service unchanged after the
/// receiving HTTP server decodes the path.

#include <string>

namespace outpost
{
namespace common
{

/// @brief Percent-encode value for use as one URL path segment.
std::string encodePathSegment(const std::string& value);

}  // namespace common
}  // namespace outpost
