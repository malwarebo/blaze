cmake_minimum_required(VERSION 3.12)
project(blaze)

# Find the curl package
find_package(CURL REQUIRED)

# Add your project executable(s) and link them with curl
add_executable(your_executable lib/http_client.cpp)  # Replace main.cpp with your source files
target_link_libraries(your_executable PRIVATE CURL::CURL)
