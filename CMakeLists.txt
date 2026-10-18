cmake_minimum_required(VERSION 4.2.3)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")

# 必须在 project() 之前设置——vcpkg 的工具链文件是被 project() 触发加载的, 它在加载时
# 读取 VCPKG_MANIFEST_FEATURES 来决定装不装 python-bindings 这个可选 feature(python3
# 编译很慢, 大部分场景用不到)。option() 读到的是当前 CMake 缓存里的值, 每次 configure
# (包括 cmake-gui 里点 Configure)都会重新走一遍这段逻辑, 所以切换这个勾选框之后再点一次
# Configure 就能让 vcpkg 增量装上/卸掉对应依赖, 不需要命令行、也不需要清空 Build/ 重来。
option(HYP_ENABLE_PYTHON_BINDINGS "Build PyHyperEngine Python bindings and the Test project that embeds them (requires compiling python3 via vcpkg, which is slow)" OFF)
if (HYP_ENABLE_PYTHON_BINDINGS)
    list(APPEND VCPKG_MANIFEST_FEATURES "python-bindings")
endif()

if (POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project ("HyperEngine")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

set(CMAKE_CXX_STANDARD 20)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # 开启多处理器编译
    add_compile_options(/MP)
endif()

# 引擎只用 Vulkan/WebGPU, 不需要 GLFW 自带的桌面 GL 头文件包含逻辑——不加这个宏的话
# <GLFW/glfw3.h> 默认会 #include <GL/gl.h>, 这个头文件在很多 Linux 发行版上并不
# 随系统自带(需要额外装 libgl-dev), 纯属不必要的系统依赖。
add_definitions(-DGLFW_INCLUDE_NONE)

add_subdirectory("ThirdParty")
add_subdirectory("Source/Runtime")
add_subdirectory("Source/Editor")

