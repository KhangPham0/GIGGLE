# The core library: GIGGLE's domain types and serialization.
# Depends on nothing but the standard library and nlohmann_json --
# no ROOT, no GUI.
add_library(giggle_core STATIC
    FitModel.cpp
    Serialization.cpp
    Version.cpp
)

target_include_directories(giggle_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(giggle_core PUBLIC nlohmann_json::nlohmann_json)
target_compile_definitions(giggle_core PRIVATE GIGGLE_VERSION="${PROJECT_VERSION}")
