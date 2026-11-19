# Hand-built core primitives. By project rule this library links no third-party code.
add_library(outpost_common
    src/consistentHash.cpp
    src/lruCache.cpp
    src/tokenBucket.cpp
    src/circuitBreaker.cpp
    src/wal.cpp
)

target_include_directories(outpost_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_package(Threads REQUIRED)
target_link_libraries(outpost_common PUBLIC Threads::Threads)

if(BUILD_TESTING)
    add_executable(common_tests
        tests/testWal.cpp
    )
    target_link_libraries(common_tests PRIVATE
        outpost_common
        GTest::gtest_main
    )
    add_test(NAME common_tests COMMAND common_tests)
endif()
