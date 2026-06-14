cmake_minimum_required(VERSION 3.10)
project(ispc_texcomp)

# 1. 创建导入目标 (SHARED 表示它是动态库)
add_library(ispc_texcomp::ispc_texcomp SHARED IMPORTED GLOBAL)

# 2. 设置头文件路径 (INTERFACE，因为库是现成的)
set_target_properties(ispc_texcomp::ispc_texcomp PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

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