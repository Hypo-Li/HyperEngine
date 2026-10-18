cmake_minimum_required(VERSION 4.2.3)
project(ispc_texcomp)

# 1. 创建导入目标 (SHARED 表示它是动态库)
add_library(ispc_texcomp::ispc_texcomp SHARED IMPORTED GLOBAL)

# 2. 设置头文件路径 (INTERFACE，因为库是现成的)
set_target_properties(ispc_texcomp::ispc_texcomp PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if (WIN32)
    # 3. 设置 Release 版本的路径
    set_target_properties(ispc_texcomp::ispc_texcomp PROPERTIES
        # 链接用的 .lib
        IMPORTED_IMPLIB_RELEASE   "${CMAKE_CURRENT_SOURCE_DIR}/lib/ispc_texcomp.lib"
        # 运行用的 .dll
        IMPORTED_LOCATION_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/bin/ispc_texcomp.dll"
    )

    # 4. 设置 Debug 版本的路径
    set_target_properties(ispc_texcomp::ispc_texcomp PROPERTIES
        # 链接用的 .lib (在 debug/lib 下)
        IMPORTED_IMPLIB_DEBUG     "${CMAKE_CURRENT_SOURCE_DIR}/debug/lib/ispc_texcomp.lib"
        # 运行用的 .dll (在 debug/bin 下)
        IMPORTED_LOCATION_DEBUG   "${CMAKE_CURRENT_SOURCE_DIR}/debug/bin/ispc_texcomp.dll"
    )
elseif (LINUX)
    # Linux 下这个库是从源码(GameTechDev/ISPCTextureCompressor)用 g++ 单份编译出来的
    # .so, 没有区分 debug/release 的 CRT ABI 问题(不像 MSVC 的 /MD 与 /MDd), 也没有
    # 单独的导入库概念, 一份 .so 同时当链接期和运行期产物, Debug/Release 都用它。
    #
    # 这份 .so 编译时没有嵌入 soname(手工用 g++ 直接编的, 没加 -Wl,-soname,...)。
    # 不设 IMPORTED_NO_SONAME 的话, CMake/链接器在缺 soname 时会退化成把这个 .so 的
    # 完整构建期绝对路径记进可执行文件的 DT_NEEDED 条目, 而不是记文件名——这样即使
    # 运行时把 .so 拷到可执行文件旁边、RPATH 设成 $ORIGIN, 动态链接器依然会去找那个
    # 写死的构建期路径, 换一台机器就直接找不到、启动失败。显式声明"这个库没有
    # soname"之后, 链接器改成只记文件名, 交给 RPATH/RUNPATH 在运行时解析, 可执行文件
    # 才能真正脱离构建机器挪动。
    set_target_properties(ispc_texcomp::ispc_texcomp PROPERTIES
        IMPORTED_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/linux/libispc_texcomp.so"
        IMPORTED_NO_SONAME TRUE
    )
endif()
