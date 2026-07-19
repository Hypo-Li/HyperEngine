find_package(spdlog CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(xxHash CONFIG REQUIRED)

add_smart_library(Core)

target_link_libraries(Core PUBLIC
    spdlog::spdlog
    glm::glm-header-only
    nlohmann_json::nlohmann_json
    xxHash::xxhash
)

target_compile_options(Core PUBLIC /utf-8)

# Core 里的每个反射类型都靠 REGISTER_CLASS 展开出的静态成员初始化把自己注册进 Reflection——
# 这是一个"没人直接引用这个类,链接器就没理由把它的 .obj 从静态库里挑出来"的自注册模式,
# 按需抽取的默认链接行为会导致完全没被直接调用过的类(只通过反序列化/多态间接用到的)
# 注册代码根本不会被链接进最终程序。/WHOLEARCHIVE 强制把这个库的每个 .obj 都链接进去,
# 保证所有 REGISTER_CLASS 都会执行。挂在 INTERFACE 上,链接 Core 的目标会自动带上这个选项。
if (MSVC)
    target_link_options(Core INTERFACE "/WHOLEARCHIVE:$<TARGET_FILE:Core>")
endif()
