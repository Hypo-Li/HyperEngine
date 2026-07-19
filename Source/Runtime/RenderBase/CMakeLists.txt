find_package(slang CONFIG REQUIRED)

add_smart_library(RenderBase)

target_link_libraries (RenderBase PUBLIC
    Core
    Platform
    RHI
    Vulkan
    WebGPU
    slang::slang
    dds_image::dds_image
)
