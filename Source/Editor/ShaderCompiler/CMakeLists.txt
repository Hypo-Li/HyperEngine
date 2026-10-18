find_package(slang CONFIG REQUIRED)

add_smart_library(ShaderCompiler)

target_link_libraries(ShaderCompiler
    PUBLIC RenderBase
    PRIVATE slang::slang
)
