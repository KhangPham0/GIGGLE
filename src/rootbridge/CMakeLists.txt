# The rootbridge library: the only part of GIGGLE that includes ROOT
# headers. Everything else talks to it through the interfaces in core.
# ROOT is linked privately, so a ROOT include leaking into another target
# fails to compile.
add_library(giggle_rootbridge STATIC
    RootFileSource.cpp
    RootFitEngine.cpp
)

target_include_directories(giggle_rootbridge PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(giggle_rootbridge
    PUBLIC giggle_core
    PRIVATE ROOT::Core ROOT::RIO ROOT::Hist ROOT::MathCore
)
