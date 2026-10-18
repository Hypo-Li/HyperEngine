# cmake -P script mode: 被 CopyLinuxRuntimeDeps.cmake 里的 copy_linux_runtime_deps() 通过
# POST_BUILD 调用。用 file(GLOB) 而不是把具体版本号硬编码进 CMakeLists.txt, 避免 vcpkg
# 升级 slang 版本号之后跟着失效。FOLLOW_SYMLINK_CHAIN 保证 libslang.so 这类版本无关的符号
# 链接、以及它指向的真实 .so 一并拷过去——slang 运行时按不带版本号的名字 dlopen 内部模块,
# 只拷真实文件、漏掉符号链接的话在新目录里会找不到。
#
# Required -D arguments: SLANG_LIB_DIR, ISPC_SO, DEST_DIR

if(NOT DEFINED SLANG_LIB_DIR OR NOT DEFINED ISPC_SO OR NOT DEFINED DEST_DIR)
    message(FATAL_ERROR "CopyLinuxRuntimeDepsImpl.cmake requires SLANG_LIB_DIR, ISPC_SO, DEST_DIR")
endif()

file(GLOB SLANG_SO_FILES "${SLANG_LIB_DIR}/libslang*.so*")
foreach(SO_FILE ${SLANG_SO_FILES})
    file(COPY "${SO_FILE}" DESTINATION "${DEST_DIR}" FOLLOW_SYMLINK_CHAIN)
endforeach()

if(EXISTS "${ISPC_SO}")
    file(COPY "${ISPC_SO}" DESTINATION "${DEST_DIR}")
endif()
