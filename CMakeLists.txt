cmake_minimum_required(VERSION 3.14)
project(blaze VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find libcurl for HTTP requests
find_package(CURL REQUIRED)

# Main library
add_library(blaze_http 
    lib/http_client.cpp
)

target_include_directories(blaze_http PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/lib
)

target_link_libraries(blaze_http PUBLIC 
    CURL::libcurl
)

# Example executable
add_executable(blaze_example
    examples/main.cpp
)

target_link_libraries(blaze_example PRIVATE
    blaze_http
)

# Tests
enable_testing()
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

add_executable(blaze_tests
    tests/test_http_client.cpp
)

target_link_libraries(blaze_tests PRIVATE
    blaze_http
    gtest
    gtest_main
    gmock
)

include(GoogleTest)
gtest_discover_tests(blaze_tests)
