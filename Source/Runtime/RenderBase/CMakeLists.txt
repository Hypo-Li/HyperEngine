find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

find_package(slang CONFIG REQUIRED)

add_smart_library(RenderBase)

target_include_directories(RenderBase PUBLIC ${Vulkan_INCLUDE_DIR})

target_link_libraries (RenderBase PUBLIC
    Core
    Platform
    volk::volk
    GPUOpen::VulkanMemoryAllocator
    slang::slang
)
