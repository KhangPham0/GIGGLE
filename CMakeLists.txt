cmake_minimum_required(VERSION 3.16)
project(giggle VERSION 0.1.0 LANGUAGES C CXX)

# The file dialog library (nfd) uses Objective-C on macOS.
if(APPLE)
    enable_language(OBJC)
endif()

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
# No version floor is asserted: GIGGLE is developed and tested against one ROOT
# version (see docs/building.md), and other versions are simply untested.
find_package(ROOT REQUIRED)

# A ROOT built with a different C++ standard than this project is usually
# fine: ROOT's imported targets carry their own standard, so the rootbridge
# library is compiled with it automatically (Homebrew and conda-forge ROOT,
# both C++20, build and run this way). Say so, and say what to change if it
# ever does cause trouble, but let the build proceed.
if(DEFINED ROOT_CXX_STANDARD AND NOT ROOT_CXX_STANDARD STREQUAL CMAKE_CXX_STANDARD)
    message(WARNING
        "ROOT was built with C++${ROOT_CXX_STANDARD} but this project is set "
        "to C++${CMAKE_CXX_STANDARD}. This is usually harmless: the one "
        "library that includes ROOT headers is compiled as "
        "C++${ROOT_CXX_STANDARD} automatically. Only if the build or the "
        "application misbehaves, change the set(CMAKE_CXX_STANDARD ...) line "
        "near the top of CMakeLists.txt in the project root to "
        "${ROOT_CXX_STANDARD}.")
endif()

add_subdirectory(vendor)
add_subdirectory(src)

# The test suite is an internal development aid, not part of the published
# tree; build it only when tests/ is present.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
