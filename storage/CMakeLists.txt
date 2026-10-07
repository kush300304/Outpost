add_executable(storage_server
    src/main.cpp
    src/kvStore.cpp
)

target_include_directories(storage_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(storage_server PRIVATE
    outpost_common
    httplib::httplib
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(BUILD_TESTING)
    add_executable(storage_tests
        tests/testKvStore.cpp
    )
    target_link_libraries(storage_tests PRIVATE
        outpost_common
        GTest::gtest_main
    )
    add_test(NAME storage_tests COMMAND storage_tests)
endif()
