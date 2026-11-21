/// @file testHttpCache.cpp
/// @brief Unit tests for the entity-tag helpers in common/httpCache.h.

#include "common/httpCache.h"

#include <gtest/gtest.h>

using outpost::common::etagMatches;
using outpost::common::makeEtag;

TEST(HttpCacheTest, EtagIsTheQuotedWriteIdentity)
{
    EXPECT_EQ(makeEtag(1795200000123456ull, "edge-1"), "\"1795200000123456-edge-1\"");
}

TEST(HttpCacheTest, IfNoneMatchMatchesExactListWildcardAndWeakTags)
{
    const std::string tag = makeEtag(42, "edge-2");
    EXPECT_TRUE(etagMatches("\"42-edge-2\"", tag));
    EXPECT_TRUE(etagMatches("\"1-edge-1\", \"42-edge-2\"", tag));
    EXPECT_TRUE(etagMatches("*", tag));
    EXPECT_TRUE(etagMatches("W/\"42-edge-2\"", tag));
}

TEST(HttpCacheTest, IfNoneMatchDoesNotMatchOtherOrMissingTags)
{
    const std::string tag = makeEtag(42, "edge-2");
    EXPECT_FALSE(etagMatches("\"43-edge-2\"", tag));
    EXPECT_FALSE(etagMatches("\"42-edge-3\"", tag));
    EXPECT_FALSE(etagMatches("", tag));
    EXPECT_FALSE(etagMatches("*", ""));
}
