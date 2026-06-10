add_subdirectory(core)

add_executable(giggle
    main.cpp
    ui/App.cpp
    ui/Fonts.cpp
    ui/Theme.cpp
    ui/panels/FileTreePanel.cpp
    ui/panels/FitModelPanel.cpp
    ui/panels/PlotPanel.cpp
    ui/panels/ResultsPanel.cpp
)

# So sources can include project headers relative to src/, e.g. "ui/App.h".
target_include_directories(giggle PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

find_package(OpenGL REQUIRED)

# implot links imgui publicly, so imgui's headers and library come with it.
target_link_libraries(giggle PRIVATE implot OpenGL::GL)
