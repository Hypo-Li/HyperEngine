#pragma once
#include "Memory.h"

namespace hyp::vk
{
    class Buffer;
    class BufferView : public Refable
    {
        friend class Buffer;
    public:
        virtual ~BufferView();

        VkBufferView getHandle() const
        {
            return mBufferView;
        }

    private:
        BufferView(Buffer& buffer, VkBufferView bufferView);

    private:
        Buffer& mBuffer;
        VkBufferView mBufferView{ VK_NULL_HANDLE };
    };

    class Buffer : public Refable
    {
    public:
        struct CreateInfo
        {
            VkDeviceSize size;
            VkBufferUsageFlags usage;
        };

        Buffer(
            const Device& device,
            const CreateInfo& createInfo,
            VmaMemoryUsage memoryUsage,
            VmaAllocationCreateFlags allocationFlags = 0
        );

        virtual ~Buffer() noexcept;

        const Device& getDevice() const
        {
            return mDevice;
        }

        VkBuffer getHandle() const
        {
            return mBuffer;
        }

        VkDeviceSize getSize() const
        {
            return mSize;
        }

        VkMemoryRequirements getMemoryRequirements() const noexcept;

        bool bindMemory(MemoryAllocation* memoryAllocation) noexcept;

        MemoryAllocation* getMemory() const noexcept
        {
            return mMemory;
        }

        VkDeviceAddress getDeviceAddress() const noexcept;

        struct ViewInfo
        {
            VkFormat format;
            VkDeviceSize offset;
            VkDeviceSize range;
        };

        BufferView* createView(const ViewInfo& viewInfo);

        bool destroyView(BufferView& bufferView);

        const std::vector<TRef<BufferView>>& getViews() const noexcept
        {
            return mViews;
        }

    private:
        const Device& mDevice;
        VkBuffer mBuffer{ VK_NULL_HANDLE };
        VkDeviceSize mSize;
        TRef<MemoryAllocation> mMemory;

        std::vector<TRef<BufferView>> mViews;
    };
}
