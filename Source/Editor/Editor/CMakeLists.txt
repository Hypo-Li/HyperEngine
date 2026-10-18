file(GLOB_RECURSE HEADER_FILES "*.h")
file(GLOB_RECURSE SOURCE_FILES "*.cpp")

set(AllFiles ${HEADER_FILES} ${SOURCE_FILES})

foreach(fileItem ${AllFiles})
    get_filename_component(PARENT_DIR "${fileItem}" DIRECTORY)
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")
    string(REPLACE "/" "\\" GROUP "${GROUP}")
    set(GROUP "${GROUP}")
    source_group("${GROUP}" FILES "${fileItem}")
endforeach()

add_executable (Editor ${HEADER_FILES} ${SOURCE_FILES})

# Texture2DImportDialog.cpp/TextureCubeImportDialog.cpp 里 EXR 导入用的 tinyexr——这个 vcpkg port 跟 stb/tinygltf 不一样,
# 不是纯头文件库: 它把实现体预编译成了 tinyexr.lib, 导出一份带 "unofficial::" 命名空间的 CMake
# config(exr_reader.hh 这类内部实现头文件只在 vcpkg 编译 tinyexr.lib 那一步自己用得到, 不给
# 消费者装, 所以消费者绝不能自己 #define TINYEXR_IMPLEMENTATION 去现场编译一遍实现体, 只能链接
# 这份已经编译好的库)。miniz::miniz 已经是这个导入 target 的 INTERFACE_LINK_LIBRARIES, 不需要
# 再手动 find_package(miniz) 一遍。
find_package(tinyexr CONFIG REQUIRED)
target_link_libraries (Editor Engine AssetInterchange FontUtility HypImGui LanguageIntelligence MeshUtility ShaderCompiler TextureUtility unofficial::tinyexr::tinyexr)

# AssetBrowser.cpp 里加载图标用的 stb_image.h 声明——实现体不在这里定义(已经在
# AssetInterchange::GLTFLoader.cpp 里 #define STB_IMAGE_IMPLEMENTATION 过一次, 链接期从
# AssetInterchange 静态库里解析符号即可, 这里只需要把头文件搜索路径加上
find_path(STB_INCLUDE_DIRS "stb_image.h")
target_include_directories(Editor PRIVATE ${STB_INCLUDE_DIRS})

if (WIN32)
    # 参见 Source/Test/RenderTest/CMakeLists.txt 里的同款注释——这段只在 Windows 上需要。
    set(SLANG_DLL "$<IF:$<CONFIG:Debug>,${PROJECT_BINARY_DIR}/vcpkg_installed/x64-windows/debug/bin/slang-glslang.dll,${PROJECT_BINARY_DIR}/vcpkg_installed/x64-windows/bin/slang-glslang.dll>")

    set(ISPC_DLL "$<IF:$<CONFIG:Debug>,${CMAKE_SOURCE_DIR}/ThirdParty/ispc_texcomp/debug/bin/ispc_texcomp.dll,${CMAKE_SOURCE_DIR}/ThirdParty/ispc_texcomp/bin/ispc_texcomp.dll>")

    # slangd.exe (Slang's own language server) is a vcpkg tools/ binary, not a per-config build
    # artifact — one copy serves both Debug and Release, and its DLL dependencies (gfx.dll,
    # slang-glsl-module.dll, slang-llvm.dll, slang-rt.dll, slang.dll) are the same files vcpkg
    # hardlinks into debug/bin (no separate debug/release build for these components), so copying
    # them here doesn't create a version mismatch with slang-compiler.dll/slang-glslang.dll, which
    # are already copied below. SlangProvider's SlangdBackend looks for slangd.exe next to the
    # running executable (Platform::getExecutableDir()), so it needs to land here rather than
    # being resolved via a build-directory-relative path at runtime.
    set(SLANGD_TOOLS_DIR "${PROJECT_BINARY_DIR}/vcpkg_installed/x64-windows/tools/shader-slang")

    add_custom_command(
        TARGET Editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SLANG_DLL}" "$<TARGET_FILE_DIR:Editor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ISPC_DLL}" "$<TARGET_FILE_DIR:Editor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SLANGD_TOOLS_DIR}/slangd.exe" "$<TARGET_FILE_DIR:Editor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SLANGD_TOOLS_DIR}/gfx.dll" "$<TARGET_FILE_DIR:Editor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SLANGD_TOOLS_DIR}/slang-glsl-module.dll" "$<TARGET_FILE_DIR:Editor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SLANGD_TOOLS_DIR}/slang-llvm.dll" "$<TARGET_FILE_DIR:Editor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SLANGD_TOOLS_DIR}/slang-rt.dll" "$<TARGET_FILE_DIR:Editor>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SLANGD_TOOLS_DIR}/slang.dll" "$<TARGET_FILE_DIR:Editor>"
        COMMENT "Copying runtime dlls ($<CONFIG>) to build directory..."
    )
elseif (LINUX)
    copy_linux_runtime_deps(Editor)
endif()

generate_dev_config(Editor)
