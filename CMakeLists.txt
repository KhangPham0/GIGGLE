cmake_minimum_required(VERSION 3.16)
project(giggle VERSION 0.1.0 LANGUAGES C CXX)

# C++17 to match the standard of the ROOT builds we will link against.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# So editors and clang tooling can find the vendored headers.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(vendor)
add_subdirectory(src)
