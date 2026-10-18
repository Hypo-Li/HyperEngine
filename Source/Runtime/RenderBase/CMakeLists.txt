add_smart_library(RenderBase)

# TextureClearer 使用 PassShaders 预编译出来的 ClearStorageTexture_*.generated.h,
# add_dependencies 保证编译 RenderBase 之前这些头文件已经生成好
add_dependencies(RenderBase PassShaders)
target_include_directories(RenderBase PUBLIC "${CMAKE_BINARY_DIR}/GeneratedShaders/$<CONFIG>")

target_link_libraries (RenderBase PUBLIC
    Core
    Platform
    RHI
    Vulkan
    WebGPU
    dds_image::dds_image
)
