add_smart_library(Vulkan)

find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

target_link_libraries(Vulkan PUBLIC
    RHI
    volk::volk
    GPUOpen::VulkanMemoryAllocator
)
