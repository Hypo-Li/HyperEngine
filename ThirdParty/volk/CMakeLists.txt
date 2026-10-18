cmake_minimum_required(VERSION 4.2.3)
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
elseif(LINUX)
    # Linux 下可能有多种窗口系统，按需选择
    # PRIVATE(而不是 PUBLIC): <X11/Xlib.h> 会把 None/Bool/Status/Always 等一大堆常见英文
    # 单词定义成裸整数宏, 和引擎自己的枚举值/标识符冲突。这几个宏只有 volk.c 自己(加载
    # vkCreateXlibSurfaceKHR 等平台函数指针要用)和真正调用这些 Xlib/Wayland surface
    # 函数的 Vulkan/Context.cpp、Vulkan/Swapchain.cpp 需要, 不应该通过 PUBLIC 自动传给
    # 每一个链接 volk::volk 的消费者, 后两个文件自己在 include volk.h 之前局部 #define。
    # X11 (Xlib)
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_XLIB_KHR)
    # 或者 XCB
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_XCB_KHR)
    # Wayland
    target_compile_definitions(volk PRIVATE VK_USE_PLATFORM_WAYLAND_KHR)
endif()

set_target_properties(volk PROPERTIES
    C_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
)
