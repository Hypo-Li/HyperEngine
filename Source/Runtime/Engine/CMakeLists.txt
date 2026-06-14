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
