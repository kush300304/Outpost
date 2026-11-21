#pragma once

/// @file requestLimits.h
/// @brief Key and value size limits shared by every service that accepts writes.
///
/// Edges enforce them before accepting (or queueing) a write and write-api enforces them again,
/// so a write queued during an outage can never be rejected later for its size.

#include <cstddef>
#include <string>

namespace outpost
{
namespace common
{

constexpr std::size_t kMaxKeyBytes = 256;            ///< Longest accepted key, in bytes.
constexpr std::size_t kMaxValueBytes = 1024 * 1024;  ///< Largest accepted value, in bytes.
constexpr std::size_t kMaxWriterIdBytes = 64;         ///< Longest accepted writer id, in bytes.

/// @brief Whether id is a valid writer id: 1–64 characters from [A-Za-z0-9._-].
///
/// Writer ids become part of entity tags ("<version>-<writerId>"), so they must not contain quotes,
/// spaces or commas.
inline bool isValidWriterId(const std::string& id)
{
    bool valid = !id.empty() && id.size() <= kMaxWriterIdBytes;
    for (std::size_t i = 0; valid && i < id.size(); ++i)
    {
        char c = id[i];
        valid = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                c == '.' || c == '_' || c == '-';
    }
    return valid;
}

}  // namespace common
}  // namespace outpost
