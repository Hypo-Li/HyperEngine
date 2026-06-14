#pragma once
#include "Device.h"

#define NOMINMAX
#define VK_NO_PROTOTYPES
#include <vma/vk_mem_alloc.h>

namespace hyp::vk
{
    class MemoryAllocator;

    class MemoryAllocation : public Refable
    {
        friend class MemoryAllocator;
    public:
        MemoryAllocation(const MemoryAllocator& allocator, VmaAllocation allocation) noexcept;

        MemoryAllocation(const MemoryAllocator& allocator, const VkMemoryRequirements& memRequirements, const VmaAllocationCreateInfo& createInfo) noexcept;

        ~MemoryAllocation() noexcept;

        const MemoryAllocator& getAllocator() const noexcept
        {
            return mAllocator;
        }

        VmaAllocation getHandle() const noexcept
        {
            return mAllocation;
        }

        const VmaAllocationInfo& getInfo() const noexcept
        {
            return mInfo;
        }

        VkMemoryPropertyFlags getProperties() const noexcept
        {
            return mProperties;
        }

        void* map() const noexcept;

        void unmap() const noexcept;

        void flush(VkDeviceSize offset, VkDeviceSize size) const noexcept;

        bool bindBuffer(VkBuffer buffer) const noexcept;

        bool bindImage(VkImage image) const noexcept;

    private:
        const MemoryAllocator& mAllocator;
        VmaAllocation mAllocation;
        VmaAllocationInfo mInfo;
        VkMemoryPropertyFlags mProperties;
    };

    class MemoryAllocator
    {
    public:
        MemoryAllocator(const Device& device, VmaAllocatorCreateFlags flags = 0) noexcept;
        ~MemoryAllocator() noexcept;

        VmaAllocator getHandle() const noexcept
        {
            return mAllocator;
        }

    private:
        VmaAllocator mAllocator;
    };
}
