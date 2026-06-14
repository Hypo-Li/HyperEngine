#define VMA_IMPLEMENTATION
#include "Memory.h"

namespace hyp::vk
{
    MemoryAllocation::MemoryAllocation(const MemoryAllocator& allocator, VmaAllocation allocation) noexcept
        : mAllocator(allocator)
    {
        mAllocation = allocation;
        vmaGetAllocationInfo(mAllocator.getHandle(), allocation, &mInfo);
        vmaGetAllocationMemoryProperties(mAllocator.getHandle(), mAllocation, &mProperties);
    }

    MemoryAllocation::MemoryAllocation(
        const MemoryAllocator& allocator,
        const VkMemoryRequirements& memRequirements,
        const VmaAllocationCreateInfo& createInfo
    ) noexcept
        : mAllocator(allocator)
    {
        vmaAllocateMemory(mAllocator.getHandle(), &memRequirements, &createInfo, &mAllocation, &mInfo);
        vmaGetAllocationMemoryProperties(mAllocator.getHandle(), mAllocation, &mProperties);
    }

    MemoryAllocation::~MemoryAllocation() noexcept
    {
        vmaFreeMemory(mAllocator.getHandle(), mAllocation);
    }

    void* MemoryAllocation::map() const noexcept
    {
        void* data;
        vmaMapMemory(mAllocator.getHandle(), mAllocation, &data);
        return data;
    }

    void MemoryAllocation::unmap() const noexcept
    {
        vmaUnmapMemory(mAllocator.getHandle(), mAllocation);
    }

    void MemoryAllocation::flush(VkDeviceSize offset, VkDeviceSize size) const noexcept
    {
        vmaFlushAllocation(mAllocator.getHandle(), mAllocation, offset, size);
    }

    bool MemoryAllocation::bindBuffer(VkBuffer buffer) const noexcept
    {
        return vmaBindBufferMemory(mAllocator.getHandle(), mAllocation, buffer) == VK_SUCCESS;
    }

    bool MemoryAllocation::bindImage(VkImage image) const noexcept
    {
        return vmaBindImageMemory(mAllocator.getHandle(), mAllocation, image) == VK_SUCCESS;
    }

    MemoryAllocator::MemoryAllocator(const Device& device, VmaAllocatorCreateFlags flags) noexcept
    {
        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.flags = flags;
        allocatorInfo.physicalDevice = device.getPhysicalDevice().getHandle();
        allocatorInfo.device = device.getHandle();
        allocatorInfo.instance = device.getPhysicalDevice().getInstance().getHandle();

        VmaVulkanFunctions vulkanFunctions;
        VkResult res = vmaImportVulkanFunctionsFromVolk(&allocatorInfo, &vulkanFunctions);
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;

        vmaCreateAllocator(&allocatorInfo, &mAllocator);
    }

    MemoryAllocator::~MemoryAllocator() noexcept
    {
        vmaDestroyAllocator(mAllocator);
    }

}
