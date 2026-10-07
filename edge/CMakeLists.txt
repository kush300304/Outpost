add_executable(edge_server
    src/main.cpp
    src/edgeServer.cpp
    src/originClient.cpp
)

target_include_directories(edge_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(edge_server PRIVATE
    outpost_common
    httplib::httplib
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(BUILD_TESTING)
    add_executable(edge_tests
        tests/testConsistentHash.cpp
        tests/testLruCache.cpp
        tests/testTokenBucket.cpp
        tests/testCircuitBreaker.cpp
    )
    target_link_libraries(edge_tests PRIVATE
        outpost_common
        GTest::gtest_main
    )
    add_test(NAME edge_tests COMMAND edge_tests)
endif()
