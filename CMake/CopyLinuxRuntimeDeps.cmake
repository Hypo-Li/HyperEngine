# Linux 下把项目依赖的、非系统路径的运行时共享库(slang 系列 + ispc_texcomp)拷贝到
# 可执行文件同目录, 并把 RPATH 换成 $ORIGIN 相对路径——对齐 Windows 分支"构建后把 DLL
# 拷到 exe 旁边"的做法, 让可执行文件自包含、可随意挪动, 而不是依赖写死在 RPATH 里的
# 构建树绝对路径。vcpkg 默认 triplet(x64-linux)下大部分依赖都是静态库, 只有 slang 和
# ispc_texcomp 这两个是 .so, 需要单独处理。
function(copy_linux_runtime_deps TARGET_NAME)
    set(SLANG_LIB_DIR "$<IF:$<CONFIG:Debug>,${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux/debug/lib,${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux/lib>")

    set_target_properties(${TARGET_NAME} PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "$ORIGIN"
    )

    # 把 libstdc++/libgcc 静态链进可执行文件本体, 不依赖目标机器系统默认的
    # libstdc++.so.6。这个项目要求 C++20, 编译机器上装的 GCC 往往比目标部署机器系统
    # 自带的新(比如麒麟系统默认 GCC9, 但编译用的是额外装的 GCC11 才能满足 C++20),
    # 新版 GCC 编出来的可执行文件动态链接系统默认 libstdc++.so.6 时, 会因为目标机器
    # 那份太老、缺 GLIBCXX_x.x.xx/CXXABI_x.x.xx 符号版本而启动失败。静态链进来之后,
    # 可执行文件自带匹配的 C++ 运行库, 不再受"编译机器和运行机器 libstdc++ 版本不一致"
    # 影响。只对最终可执行文件生效(target_link_options 只在链接阶段起作用), 不影响
    # 静态库(Core/RenderBase 等)本身的编译。
    target_link_options(${TARGET_NAME} PRIVATE -static-libgcc -static-libstdc++)

    add_custom_command(
        TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND}
            "-DSLANG_LIB_DIR=${SLANG_LIB_DIR}"
            "-DISPC_SO=${CMAKE_SOURCE_DIR}/ThirdParty/ispc_texcomp/linux/libispc_texcomp.so"
            "-DDEST_DIR=$<TARGET_FILE_DIR:${TARGET_NAME}>"
            -P "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/CopyLinuxRuntimeDepsImpl.cmake"
        COMMENT "Copying runtime shared libraries ($<CONFIG>) next to ${TARGET_NAME}..."
        VERBATIM
    )
endfunction()
