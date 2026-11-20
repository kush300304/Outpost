add_library(router_lib STATIC
    src/routerServer.cpp
)
target_include_directories(router_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(router_lib PUBLIC
    outpost_common
    httplib::httplib
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_executable(router_server
    src/main.cpp
)
target_link_libraries(router_server PRIVATE
    router_lib
)

if(BUILD_TESTING)
    add_executable(router_tests
        tests/testRouterServer.cpp
    )
    target_link_libraries(router_tests PRIVATE
        router_lib
        GTest::gtest_main
    )
    add_test(NAME router_tests COMMAND router_tests)
endif()
