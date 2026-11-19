add_library(storage_lib STATIC
    src/kvStore.cpp
    src/storageHandlers.cpp
)
target_include_directories(storage_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(storage_lib PUBLIC
    outpost_common
    httplib::httplib
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

add_executable(storage_server
    src/main.cpp
)
target_link_libraries(storage_server PRIVATE
    storage_lib
)

if(BUILD_TESTING)
    add_executable(storage_tests
        tests/testKvStore.cpp
    )
    target_link_libraries(storage_tests PRIVATE
        storage_lib
        GTest::gtest_main
    )
    add_test(NAME storage_tests COMMAND storage_tests)
endif()
