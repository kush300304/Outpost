add_library(write_api_lib STATIC
    src/apiHandlers.cpp
    src/storageClient.cpp
)
target_include_directories(write_api_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(write_api_lib PUBLIC
    outpost_common
    httplib::httplib
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_executable(write_api_server
    src/main.cpp
)
target_link_libraries(write_api_server PRIVATE
    write_api_lib
)

if(BUILD_TESTING)
    add_executable(write_api_tests
        tests/testApiHandlers.cpp
    )
    target_link_libraries(write_api_tests PRIVATE
        write_api_lib
        GTest::gtest_main
    )
    add_test(NAME write_api_tests COMMAND write_api_tests)
endif()
