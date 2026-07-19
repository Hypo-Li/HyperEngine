add_smart_library(AssetInterchange)

target_link_libraries(AssetInterchange PUBLIC Engine)

# tinygltf 在 vcpkg 里是纯头文件库, 没有导出 CMake config, 用 find_path 找它的头文件目录
# (根 CMakeLists.txt 里之前注释掉的 Test target 用的就是这个写法); stb_image.h 是 tinygltf
# 内部按需 #include 的依赖, 装在同一个 vcpkg include 目录下, 不用再单独 find_path 一次
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")
target_include_directories(AssetInterchange PRIVATE ${TINYGLTF_INCLUDE_DIRS})
