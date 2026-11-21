# Hand-built core primitives. By project rule this library links no third-party code.
add_library(outpost_common
    src/consistentHash.cpp
    src/lruCache.cpp
    src/tokenBucket.cpp
    src/circuitBreaker.cpp
    src/wal.cpp
    src/urlEncode.cpp
    src/shutdownSignal.cpp
    src/httpCache.cpp
)

target_include_directories(outpost_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
target_link_libraries(outpost_common PUBLIC Threads::Threads)

if(BUILD_TESTING)
    add_executable(common_tests
        tests/testConsistentHash.cpp
        tests/testLruCache.cpp
        tests/testTokenBucket.cpp
        tests/testCircuitBreaker.cpp
        tests/testWal.cpp
        tests/testUrlEncode.cpp
        tests/testHttpCache.cpp
        tests/testShutdownSignal.cpp
    )
    target_link_libraries(common_tests PRIVATE
        outpost_common
        GTest::gtest_main
    )
    add_test(NAME common_tests COMMAND common_tests)
endif()
