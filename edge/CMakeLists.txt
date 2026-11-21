add_library(edge_lib STATIC
    src/edgeServer.cpp
    src/originClient.cpp
)
target_include_directories(edge_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(edge_lib PUBLIC
    outpost_common
    httplib::httplib
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_executable(edge_server
    src/main.cpp
)
target_link_libraries(edge_server PRIVATE
    edge_lib
)

if(BUILD_TESTING)
    add_executable(edge_tests
        tests/testEdgeServer.cpp
    )
    target_link_libraries(edge_tests PRIVATE
        edge_lib
        write_api_lib
        storage_lib
        GTest::gtest_main
    )
    target_compile_definitions(edge_tests PRIVATE OUTPOST_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME edge_tests COMMAND edge_tests)
endif()
