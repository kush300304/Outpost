add_executable(write_api_server
    src/main.cpp
    src/apiHandlers.cpp
    src/storageClient.cpp
)

target_include_directories(write_api_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(write_api_server PRIVATE
    outpost_common
    httplib::httplib
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

if(BUILD_TESTING)
    add_executable(write_api_tests
        tests/testApiHandlers.cpp
    )
    target_link_libraries(write_api_tests PRIVATE
        outpost_common
        GTest::gtest_main
    )
    add_test(NAME write_api_tests COMMAND write_api_tests)
endif()
