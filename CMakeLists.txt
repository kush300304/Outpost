cmake_minimum_required(VERSION 3.16)
project(outpost VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable testing by default
option(BUILD_TESTING "Build the test suite" ON)

# Sanitizer builds: -DOUTPOST_SANITIZE=address,undefined or -DOUTPOST_SANITIZE=thread.
# Applied to every target (dependencies included) so instrumented and plain code never mix.
set(OUTPOST_SANITIZE "" CACHE STRING "Sanitizers to enable: address,undefined | thread | empty")
if(OUTPOST_SANITIZE)
    add_compile_options(-fsanitize=${OUTPOST_SANITIZE} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${OUTPOST_SANITIZE})
    if(OUTPOST_SANITIZE MATCHES "undefined")
        add_compile_options(-fno-sanitize-recover=undefined)
    endif()
endif()

include(FetchContent)

# cpp-httplib listens with a backlog of 5 by default; under concurrent connects the kernel drops
# SYNs beyond it and clients wait for a 1 s retransmit. Every target that includes httplib.h must
# see the same value, so it is set once for the whole build.
add_compile_definitions(CPPHTTPLIB_LISTEN_BACKLOG=1024)
# cpp-httplib leaves Nagle's algorithm on; on a keep-alive connection a response written in two
# parts then waits for the peer's delayed ACK (~40 ms per request). Disable it everywhere.
add_compile_definitions(CPPHTTPLIB_TCP_NODELAY=true)

# cpp-httplib (HTTP client and server)
find_package(httplib QUIET)
if(NOT httplib_FOUND)
    FetchContent_Declare(
        httplib
        GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
        GIT_TAG v0.18.3
    )
    FetchContent_MakeAvailable(httplib)
endif()

# nlohmann_json (JSON serialization)
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# spdlog (Structured and fast logging)
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# GoogleTest (Unit and integration tests)
if(BUILD_TESTING)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
    enable_testing()
endif()

# Include submodules and services
add_subdirectory(common)
add_subdirectory(storage)
add_subdirectory(write-api)
add_subdirectory(edge)
add_subdirectory(router)
