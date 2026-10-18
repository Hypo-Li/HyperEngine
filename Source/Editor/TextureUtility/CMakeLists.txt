add_smart_library(TextureUtility)

target_link_libraries(TextureUtility
    PUBLIC Engine
    PRIVATE ispc_texcomp::ispc_texcomp
    PRIVATE rgbcx
)
