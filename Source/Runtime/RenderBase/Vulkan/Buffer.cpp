#include "Buffer.h"

namespace hyp::vk
{
    BufferView::BufferView(Buffer& buffer, VkBufferView bufferView)
        : mBuffer(buffer), mBufferView(bufferView)
    {
    }

    BufferView::~BufferView()
    {
        if (mBufferView != VK_NULL_HANDLE)
        {
            mBuffer.destroyView(*this);
        }
    }

    Buffer::Buffer(
        const Device& device,
        const CreateInfo& createInfo,
        VmaMemoryUsage memoryUsage,
        VmaAllocationCreateFlags allocationFlags
    ) : mDevice(device), mSize(createInfo.size)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = createInfo.size;
        bufferInfo.usage = createInfo.usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo = {
            .flags = allocationFlags,
            .usage = memoryUsage
        };

        if (memoryUsage == VMA_MEMORY_USAGE_UNKNOWN)
        {
            vkCreateBuffer(mDevice.getHandle(), &bufferInfo, nullptr, &mBuffer);
        }
        else
        {
            VmaAllocation memoryAllocation;
            MemoryAllocator& memoryAllocator = mDevice.getMemoryAllocator();
            vmaCreateBuffer(memoryAllocator.getHandle(), &bufferInfo, &allocationInfo, &mBuffer, &memoryAllocation, nullptr);
            mMemory = new MemoryAllocation(memoryAllocator, memoryAllocation);
        }
    }

    Buffer::~Buffer() noexcept
    {
        for (BufferView* view : mViews)
        {
            vkDestroyBufferView(mDevice.getHandle(), view->getHandle(), nullptr);
            view->mBufferView = VK_NULL_HANDLE;
        }
        mViews.clear();

        vkDestroyBuffer(mDevice.getHandle(), mBuffer, nullptr);
    }

    VkMemoryRequirements Buffer::getMemoryRequirements() const noexcept
    {
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(mDevice.getHandle(), mBuffer, &memRequirements);
        return memRequirements;
    }

    bool Buffer::bindMemory(MemoryAllocation* memory) noexcept
    {
        if (mMemory == nullptr)
        {
            mMemory = memory;
            return mMemory->bindBuffer(mBuffer);
        }
        return false;
    }

    VkDeviceAddress Buffer::getDeviceAddress() const noexcept
    {
        VkBufferDeviceAddressInfo addressInfo = {};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = mBuffer;
        return vkGetBufferDeviceAddress(mDevice.getHandle(), &addressInfo);
    }

    BufferView* Buffer::createView(const ViewInfo& viewInfo)
    {
        VkBufferView bufferView;
        VkBufferViewCreateInfo viewCreateInfo = {};
        viewCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO;
        viewCreateInfo.buffer = mBuffer;
        viewCreateInfo.format = viewInfo.format;
        viewCreateInfo.offset = viewInfo.offset;
        viewCreateInfo.range = viewInfo.range;
        vkCreateBufferView(mDevice.getHandle(), &viewCreateInfo, nullptr, &bufferView);
        BufferView* newView = new BufferView(*this, bufferView);
        mViews.push_back(newView);
        return newView;
    }

    bool Buffer::destroyView(BufferView& bufferView)
    {
        auto it = std::find(mViews.begin(), mViews.end(), &bufferView);
        if (it != mViews.end())
        {
            vkDestroyBufferView(mDevice.getHandle(), bufferView.getHandle(), nullptr);
            bufferView.mBufferView = VK_NULL_HANDLE;
            mViews.erase(it);
            return true;
        }
        return false;
    }

}
