cmake_minimum_required(VERSION 3.10)
project(volk LANGUAGES C)

find_package (Vulkan REQUIRED)

# 生成静态库 volk.lib
add_library(volk STATIC volk.h volk.c)

add_library(volk::volk ALIAS volk)

target_include_directories(volk
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(volk PUBLIC Vulkan::Vulkan)

# 根据平台添加 VK_USE_PLATFORM_XXX 宏
if(WIN32)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
elseif(APPLE)
    # macOS 使用 MoltenVK
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_MACOS_MVK)
elseif(UNIX)
    # Linux 下可能有多种窗口系统，按需选择
    # X11 (Xlib)
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XLIB_KHR)
    # 或者 XCB
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_XCB_KHR)
    # Wayland
    target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WAYLAND_KHR)
endif()

set_target_properties(volk PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
)