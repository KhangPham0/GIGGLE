add_subdirectory(core)
add_subdirectory(rootbridge)

add_executable(giggle
    main.cpp
    ui/App.cpp
    ui/Fonts.cpp
    ui/ImageExport.cpp
    ui/Theme.cpp
    ui/panels/FileTreePanel.cpp
    ui/panels/FitModelPanel.cpp
    ui/panels/PlotPanel.cpp
    ui/panels/ResultsPanel.cpp
)

# So sources can include project headers relative to src/, e.g. "ui/App.h".
target_include_directories(giggle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(OpenGL REQUIRED)

# implot links imgui publicly, so imgui's headers and library come with it,
# and giggle_rootbridge brings giggle_core the same way.
target_link_libraries(giggle PRIVATE giggle_rootbridge implot nfd stb OpenGL::GL)
