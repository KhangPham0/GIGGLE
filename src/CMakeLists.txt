add_executable(giggle main.cpp)

find_package(OpenGL REQUIRED)

# implot links imgui publicly, so imgui's headers and library come with it.
target_link_libraries(giggle PRIVATE implot OpenGL::GL)
