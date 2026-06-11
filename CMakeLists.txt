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

# Locate ROOT. When it is not already on CMAKE_PREFIX_PATH, ask root-config.
# Only the rootbridge library (and the tests) may link it.
if(NOT ROOT_DIR)
    execute_process(
        COMMAND root-config --prefix
        OUTPUT_VARIABLE ROOT_PREFIX
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(ROOT_PREFIX)
        list(APPEND CMAKE_PREFIX_PATH "${ROOT_PREFIX}")
    endif()
endif()
find_package(ROOT 6.30 REQUIRED)

# A ROOT built with a different C++ standard than this project can produce
# link or runtime errors that are hard to diagnose; refuse early instead.
if(DEFINED ROOT_CXX_STANDARD AND NOT ROOT_CXX_STANDARD STREQUAL CMAKE_CXX_STANDARD)
    message(FATAL_ERROR
        "ROOT was built with C++${ROOT_CXX_STANDARD} but this project uses "
        "C++${CMAKE_CXX_STANDARD}. Set CMAKE_CXX_STANDARD to match.")
endif()

add_subdirectory(vendor)
add_subdirectory(src)

enable_testing()
add_subdirectory(tests)
