cmake_minimum_required(VERSION 4.2.3)
project(imgui)

set(IMGUI_CORE_SOURCES
    imgui.cpp
    imgui_demo.cpp
    imgui_draw.cpp
    imgui_stdlib.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_freetype.cpp
    TextEditor.cpp
)

set(IMGUI_BACKEND_SOURCES
    backend/imgui_impl_glfw.cpp
    backend/imgui_impl_vulkan.cpp
    backend/imgui_impl_wgpu.cpp
)

# 创建 ImGui 静态库
add_library (imgui STATIC ${IMGUI_CORE_SOURCES} ${IMGUI_BACKEND_SOURCES})
add_library (imgui::imgui ALIAS imgui)

target_include_directories(imgui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/backends>
)

find_package(glfw3 CONFIG REQUIRED)
find_package(Dawn CONFIG REQUIRED)
find_package(freetype CONFIG REQUIRED)

target_compile_definitions(imgui PUBLIC
    IMGUI_IMPL_VULKAN_USE_VOLK
    IMGUI_IMPL_WEBGPU_BACKEND_DAWN
    # Material Symbols 图标字体(Source/Runtime/HypImGui/IconsMaterialSymbols.h)的码点范围
    # [ICON_MIN_MS, ICON_MAX_MS] = [0xe003, 0xffffd] 大量超出 0xFFFF, 16-bit ImWchar 装不下——
    # 通过编译定义开 imconfig.h 里本来就留好的这个开关, 不直接改 ThirdParty 里的文件。
    IMGUI_USE_WCHAR32
)

target_link_libraries(imgui
    PUBLIC
        glfw
        volk::volk
        dawn::webgpu_dawn
        freetype
)

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" FILES ${IMGUI_CORE_SOURCES} ${IMGUI_BACKEND_SOURCES})
