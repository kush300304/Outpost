#pragma once

/// @file requestLimits.h
/// @brief Key and value size limits shared by every service that accepts writes.
///
/// Edges enforce them before accepting (or queueing) a write and write-api enforces them again,
/// so a write queued during an outage can never be rejected later for its size.

#include <cstddef>

namespace outpost {
namespace common {

constexpr std::size_t kMaxKeyBytes = 256;            ///< Longest accepted key, in bytes.
constexpr std::size_t kMaxValueBytes = 1024 * 1024;  ///< Largest accepted value, in bytes.

}  // namespace common
}  // namespace outpost
