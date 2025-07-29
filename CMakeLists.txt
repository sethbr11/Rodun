cmake_minimum_required(VERSION 3.31)
project(Rodun)

set(CMAKE_CXX_STANDARD 20)

# ImGui requires OpenGL and GLFW
find_package(OpenGL REQUIRED)

# Add dependencies
add_subdirectory(extern/glfw)

include_directories(
        extern/imgui
        extern/imgui/backends
        extern/glfw/include
        /opt/homebrew/include
)

link_directories(/opt/homebrew/lib)

file(GLOB IMGUI_SRC
        extern/imgui/*.cpp
        extern/imgui/backends/imgui_impl_glfw.cpp
        extern/imgui/backends/imgui_impl_opengl3.cpp
)

add_executable(Rodun
        src/main.cpp
        ${IMGUI_SRC}
        src/app.cpp
        src/app.h
        src/optimizer.cpp
        src/optimizer.h
        src/pdf_export.cpp
        src/pdf_export.h
        src/utils.cpp
        src/utils.h
)

target_link_libraries(Rodun
        glfw
        OpenGL::GL
        hpdf
)

# Additional frameworks for MacOS
if(APPLE)
    target_link_libraries(Rodun
            "-framework Cocoa"
            "-framework OpenGL"
            "-framework IOKit"
            "-framework CoreVideo")
endif()
