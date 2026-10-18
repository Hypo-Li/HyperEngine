#pragma once
#include <RHI/Memory.h>

#include "VolkInclude.h"
#define NOMINMAX
#define VK_NO_PROTOTYPES
// vcpkg 的 vulkan-memory-allocator port 在不同 triplet 下头文件安装路径不一致:
// x64-windows 装到 include/vma/vk_mem_alloc.h, x64-linux 直接装到 include/vk_mem_alloc.h。
#ifdef _WIN32
#include <vma/vk_mem_alloc.h>
#else
#include <vk_mem_alloc.h>
#endif

namespace hyp::vk
{
    inline VmaAllocationCreateInfo makeVmaAllocationCreateInfo(rhi::MemoryType memoryType)
    {
        VmaAllocationCreateInfo info = {};
        switch (memoryType)
        {
        case rhi::MemoryType::Gpu_Only:
            info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            break;
        case rhi::MemoryType::Cpu_To_Gpu:
            info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        case rhi::MemoryType::Gpu_To_Cpu:
            info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
            info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
            break;
        default:
            break;
        }
        return info;
    }
}
