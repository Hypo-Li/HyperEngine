add_smart_library(Engine)

find_package(Jolt CONFIG REQUIRED)

target_link_libraries (Engine
    PUBLIC
        Core
        Platform
        RenderBase
        Renderer
        ispc_texcomp::ispc_texcomp
        Jolt::Jolt
)

# 原因同 Core/CMakeLists.txt——Engine 里的 Texture2D/TextureCube/Mesh/Material 等一大批类型
# 也是靠 REGISTER_CLASS 自注册的,同样需要强制整库链接。
if (MSVC)
    target_link_options(Engine INTERFACE "/WHOLEARCHIVE:$<TARGET_FILE:Engine>")
endif()
