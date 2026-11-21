/// @file httpCache.cpp
/// @brief Entity-tag construction and If-None-Match matching.

#include "common/httpCache.h"

#include <sstream>

namespace outpost
{
namespace common
{

std::string makeEtag(uint64_t version, const std::string& writerId)
{
    return "\"" + std::to_string(version) + "-" + writerId + "\"";
}

bool etagMatches(const std::string& ifNoneMatch, const std::string& etag)
{
    bool matched = false;
    std::stringstream candidates(ifNoneMatch);
    std::string candidate;
    while (!matched && !etag.empty() && std::getline(candidates, candidate, ','))
    {
        candidate.erase(0, candidate.find_first_not_of(" \t"));
        candidate.erase(candidate.find_last_not_of(" \t") + 1);
        if (candidate.rfind("W/", 0) == 0)
        {
            candidate.erase(0, 2);
        }
        matched = candidate == "*" || candidate == etag;
    }
    return matched;
}

}  // namespace common
}  // namespace outpost
