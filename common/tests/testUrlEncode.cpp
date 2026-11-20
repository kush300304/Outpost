/// @file testUrlEncode.cpp
/// @brief Unit tests for outpost::common::encodePathSegment.

#include <gtest/gtest.h>

#include <string>

#include "common/urlEncode.h"

using outpost::common::encodePathSegment;

TEST(UrlEncodeTest, KeepsUnreservedCharacters)
{
    EXPECT_EQ(encodePathSegment("AZaz09-._~"), "AZaz09-._~");
}

TEST(UrlEncodeTest, EncodesReservedAndSpecialCharacters)
{
    EXPECT_EQ(encodePathSegment("a?b c/%#+&="), "a%3Fb%20c%2F%25%23%2B%26%3D");
}

TEST(UrlEncodeTest, EncodesUtf8BytesIndividually)
{
    EXPECT_EQ(encodePathSegment("\xC3\xA9"), "%C3%A9");
}

TEST(UrlEncodeTest, EmptyStaysEmpty)
{
    EXPECT_EQ(encodePathSegment(""), "");
}
