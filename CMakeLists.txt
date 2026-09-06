cmake_minimum_required(VERSION 3.14)
project(blaze VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Only drive tests/examples when blaze is the top-level project, so that
# add_subdirectory() consumers do not inherit them.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(BLAZE_IS_TOP_LEVEL ON)
else()
    set(BLAZE_IS_TOP_LEVEL OFF)
endif()

option(BLAZE_BUILD_TESTS "Build the blaze test suite" ${BLAZE_IS_TOP_LEVEL})
option(BLAZE_BUILD_EXAMPLES "Build the blaze examples" ${BLAZE_IS_TOP_LEVEL})
set(BLAZE_SANITIZER "" CACHE STRING "Sanitizer to build with: address, thread, or undefined")

find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

add_library(blaze_http lib/http_client.cpp)
add_library(blaze::blaze ALIAS blaze_http)

target_include_directories(blaze_http PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(blaze_http PUBLIC CURL::libcurl Threads::Threads)

set_target_properties(blaze_http PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME blaze
)

if(MSVC)
    target_compile_options(blaze_http PRIVATE /W4)
else()
    target_compile_options(blaze_http PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BLAZE_SANITIZER)
    if(MSVC)
        message(FATAL_ERROR "BLAZE_SANITIZER is not supported with MSVC")
    endif()
    add_compile_options(-fsanitize=${BLAZE_SANITIZER} -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=${BLAZE_SANITIZER})
endif()

# ---------------------------------------------------------------------------
# Install / package config
# ---------------------------------------------------------------------------

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS blaze_http
    EXPORT blazeTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
    lib/blaze.hpp
    lib/types.hpp
    lib/task.hpp
    lib/http_client.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/blaze
)

install(EXPORT blazeTargets
    FILE blazeTargets.cmake
    NAMESPACE blaze::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/blaze
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/blazeConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/blazeConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/blaze
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/blazeConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/blazeConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/blazeConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/blaze
)

# ---------------------------------------------------------------------------
# Examples and tests
# ---------------------------------------------------------------------------

if(BLAZE_BUILD_EXAMPLES)
    add_executable(blaze_example examples/main.cpp)
    target_link_libraries(blaze_example PRIVATE blaze::blaze)
endif()

if(BLAZE_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(blaze_tests
        tests/test_sync.cpp
        tests/test_async.cpp
        tests/test_utils.cpp
    )

    target_link_libraries(blaze_tests PRIVATE blaze::blaze GTest::gtest_main)

    include(GoogleTest)
    gtest_discover_tests(blaze_tests)
endif()
