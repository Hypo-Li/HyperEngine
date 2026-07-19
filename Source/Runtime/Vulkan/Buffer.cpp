#include "Buffer.h"

namespace hyp::vk
{
    static VkBufferUsageFlags convertToVkBufferUsage(rhi::BufferUsage usage)
    {
        VkBufferUsageFlags result = 0;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Transfer_Src))
            result |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Transfer_Dst))
            result |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Uniform))
            result |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Storage))
            result |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Index))
            result |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Vertex))
            result |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if (enumHasAnyFlags(usage, rhi::BufferUsage::Indirect))
            result |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
        return result;
    }

    Buffer::Buffer(const Device& device, const rhi::BufferDesc& desc, rhi::MemoryType memoryType)
        : rhi::Buffer(device, desc, memoryType)
    {
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = desc.size;
        bufferInfo.usage = convertToVkBufferUsage(desc.usage);
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo = makeVmaAllocationCreateInfo(memoryType);
        vmaCreateBuffer(device.getAllocator(), &bufferInfo, &allocationInfo, &mBuffer, &mAllocation, nullptr);
    }

    Buffer::~Buffer()
    {
        auto& device = getVkDevice();
        vkDestroyBuffer(device.getHandle(), mBuffer, nullptr);
        vmaFreeMemory(device.getAllocator(), mAllocation);
    }

    void* Buffer::map()
    {
        if (getMemoryType() == rhi::MemoryType::Gpu_To_Cpu)
        {
            vmaInvalidateAllocation(
                getVkDevice().getAllocator(),
                mAllocation,
                0,
                VK_WHOLE_SIZE
            );
        }
        void* data;
        vmaMapMemory(getVkDevice().getAllocator(), mAllocation, &data);
        return data;
    }

    void Buffer::unmap()
    {
        vmaUnmapMemory(getVkDevice().getAllocator(), mAllocation);
    }

    void Buffer::setDebugName(HString name)
    {
        rhi::Buffer::setDebugName(name);

        if (vkSetDebugUtilsObjectNameEXT)
        {
            VkDebugUtilsObjectNameInfoEXT nameInfo = {};
            nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
            nameInfo.objectType = VK_OBJECT_TYPE_BUFFER;
            nameInfo.objectHandle = reinterpret_cast<uint64_t>(mBuffer);
            nameInfo.pObjectName = getDebugName().toCString();
            vkSetDebugUtilsObjectNameEXT(getVkDevice().getHandle(), &nameInfo);
        }
    }
}
