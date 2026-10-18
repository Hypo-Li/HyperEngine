find_package(spdlog CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)
find_package(zstd CONFIG REQUIRED)

add_smart_library(Core)

target_link_libraries(Core PUBLIC
    spdlog::spdlog
    glm::glm-header-only
    nlohmann_json::nlohmann_json
    xxHash::xxhash
)

target_link_libraries(Core PRIVATE
    zstd::libzstd
)

if (LINUX)
    # Core/Common/Guid.cpp 在 Linux 下用 uuid_generate() 生成 GUID(<uuid/uuid.h>)。
    # libuuid 由 vcpkg manifest 显式安装；使用其 CMake config target 可避免 pkg-config
    # 的 include/uuid 路径与代码中的 <uuid/uuid.h> 包含方式不匹配。
    find_package(unofficial-libuuid CONFIG REQUIRED)
    target_link_libraries(Core PUBLIC unofficial::UUID::uuid)
endif()

if (MSVC)
    target_compile_options(Core PUBLIC /utf-8)
endif()

# Core 里的每个反射类型都靠 REGISTER_CLASS 展开出的静态成员初始化把自己注册进 Reflection——
# 这是一个"没人直接引用这个类,链接器就没理由把它的 .obj 从静态库里挑出来"的自注册模式,
# 按需抽取的默认链接行为会导致完全没被直接调用过的类(只通过反序列化/多态间接用到的)
# 注册代码根本不会被链接进最终程序。/WHOLEARCHIVE(MSVC)或 --whole-archive(GNU/Clang)
# 强制把这个库的每个 .obj 都链接进去,保证所有 REGISTER_CLASS 都会执行。挂在 INTERFACE 上,
# 链接 Core 的目标会自动带上这个选项。
if (MSVC)
    target_link_options(Core INTERFACE "/WHOLEARCHIVE:$<TARGET_FILE:Core>")
else()
    target_link_options(Core INTERFACE "-Wl,--whole-archive" "$<TARGET_FILE:Core>" "-Wl,--no-whole-archive")
endif()
