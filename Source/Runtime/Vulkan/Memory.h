#pragma once
#include <RHI/Memory.h>

#include <volk.h>
#define NOMINMAX
#define VK_NO_PROTOTYPES
#include <vma/vk_mem_alloc.h>

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
