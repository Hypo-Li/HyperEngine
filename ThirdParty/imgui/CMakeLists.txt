cmake_minimum_required(VERSION 3.10)
project(imgui)

set(IMGUI_CORE_SOURCES
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_stdlib.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
)

set(IMGUI_BACKEND_SOURCES
    backend/imgui_impl_glfw.cpp
    backend/imgui_impl_vulkan.cpp
    backend/imgui_impl_wgpu.cpp
)

# 创建 ImGui 静态库
add_library (imgui STATIC ${IMGUI_CORE_SOURCES} ${IMGUI_BACKEND_SOURCES})
add_library (imgui::imgui ALIAS imgui)

find_package(glfw3 CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED)

target_include_directories(imgui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backends>
)

target_compile_definitions(imgui PUBLIC
    IMGUI_IMPL_VULKAN_USE_VOLK
    IMGUI_IMPL_WEBGPU_BACKEND_DAWN
)

target_link_libraries(imgui
    PUBLIC
        glfw
        volk::volk
        dawn::webgpu_dawn
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${IMGUI_CORE_SOURCES} ${IMGUI_BACKEND_SOURCES})