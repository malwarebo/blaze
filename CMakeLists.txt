cmake_minimum_required(VERSION 3.12)
project(blaze VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows-specific settings
if(WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
endif()

# Find required packages
find_package(CURL REQUIRED)

# Create library target
add_library(blaze_http
    lib/http_client.cpp
    lib/http_client.hpp
)

# Set include directories for the library
target_include_directories(blaze_http
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/lib
        ${CURL_INCLUDE_DIRS}
)

# Link dependencies
target_link_libraries(blaze_http
    PUBLIC
        ${CURL_LIBRARIES}
)

# Create example executable (optional)
option(BUILD_EXAMPLES "Build example programs" OFF)

if(BUILD_EXAMPLES)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/main.cpp")
        add_executable(blaze_example examples/main.cpp)
        target_link_libraries(blaze_example
            PRIVATE
                blaze_http
        )
    else()
        message(STATUS "Example file not found - skipping example build")
    endif()
endif()

# Install rules (optional)
install(TARGETS blaze_http
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(FILES lib/http_client.hpp
    DESTINATION include/blaze
)

# Enable testing
# enable_testing()

# Add Google
# include(FetchContent)
# FetchContent_Declare(
#     googletest
#     GIT_REPOSITORY https://github.com/google/googletest.git
#     GIT_TAG release-1.12.1
# )
# FetchContent_MakeAvailable(googletest)

# Create test executable
# add_executable(blaze_tests
#     tests/test_http_client.cpp
# )

# target_link_libraries(blaze_tests
#     PRIVATE
#         blaze_http
#         GTest::gtest
#         GTest::gtest_main
#         GTest::gmock
#         GTest::gmock_main
# )

# Register tests
# include(GoogleTest)
# gtest_discover_tests(blaze_tests)
