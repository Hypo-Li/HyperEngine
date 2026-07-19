#pragma once
#include "Device.h"

#include <RHI/Buffer.h>

namespace hyp::vk
{
    class Buffer : public rhi::Buffer
    {
    public:
        Buffer(const Device& device, const rhi::BufferDesc& desc, rhi::MemoryType memoryType);

        ~Buffer();

        const Device& getVkDevice() const noexcept
        {
            return static_cast<const Device&>(getDevice());
        }

        VkBuffer getHandle() const noexcept
        {
            return mBuffer;
        }

        void* map() override;

        void unmap() override;

        void setDebugName(HString name) override;

    private:
        VkBuffer mBuffer = VK_NULL_HANDLE;
        VmaAllocation mAllocation = VK_NULL_HANDLE;
    };
}
