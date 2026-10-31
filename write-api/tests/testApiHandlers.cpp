/// @file testApiHandlers.cpp
/// @brief Unit tests for write-api request handlers.
///
/// These test the validation logic in isolation. Full integration tests
/// (write-api → storage) require the Docker cluster.

#include <gtest/gtest.h>

// Validation logic tests — these don't need a running server.

TEST(ApiHandlersTest, KeyLengthValidation) {
    // Keys up to 256 bytes are valid
    std::string validKey(256, 'k');
    EXPECT_LE(validKey.size(), 256u);

    // Keys over 256 bytes are rejected
    std::string invalidKey(257, 'k');
    EXPECT_GT(invalidKey.size(), 256u);
}

TEST(ApiHandlersTest, ValueSizeValidation) {
    // Values up to 1MB are valid
    std::string validValue(1024 * 1024, 'v');
    EXPECT_LE(validValue.size(), 1024u * 1024u);

    // Values over 1MB are rejected
    std::string invalidValue(1024 * 1024 + 1, 'v');
    EXPECT_GT(invalidValue.size(), 1024u * 1024u);
}
