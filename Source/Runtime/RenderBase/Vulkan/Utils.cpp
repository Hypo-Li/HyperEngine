#include "Utils.h"
#include "Queue.h"
#include <fstream>

namespace hyp::vk
{
    void Utils::uploadDataToBuffer(
        const Device& device,
        Buffer& dstBuffer,
        const void* data,
        VkDeviceSize size,
        VkDeviceSize offset
    )
    {
        Buffer::CreateInfo bufferInfo(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        Buffer stagingBuffer(device, bufferInfo, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        MemoryAllocation* allocation = stagingBuffer.getMemory();
        void* mappedData = allocation->map();
        std::memcpy((void*)(uintptr_t(mappedData) + uintptr_t(offset)), data, size);
        allocation->unmap();

        CommandPool& commandPool = device.getGraphicsCommandPool();
        TRef<CommandBuffer> commandBuffer = commandPool.allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        commandBuffer->beginRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        VkBufferCopy copyRegion;
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = offset;
        copyRegion.size = size;
        commandBuffer->copyBuffer(stagingBuffer.getHandle(), dstBuffer.getHandle(), std::span<const VkBufferCopy>(&copyRegion, 1));

        commandBuffer->endRecord();

        Queue& graphicsQueue = device.getGraphicsQueue();
        graphicsQueue.submit(*commandBuffer, {}, {}, {});
        graphicsQueue.waitIdle();
    }

    void Utils::uploadDataToImage(
        const Device& device,
        Image& dstImage,
        uint32_t level,
        uint32_t layer,
        const void* data,
        VkDeviceSize size
    )
    {
        Buffer::CreateInfo bufferInfo(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        Buffer stagingBuffer(device, bufferInfo, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        MemoryAllocation* allocation = stagingBuffer.getMemory();
        void* mappedData = allocation->map();
        std::memcpy(mappedData, data, size);
        allocation->unmap();

        CommandPool& commandPool = device.getGraphicsCommandPool();
        TRef<CommandBuffer> commandBuffer = commandPool.allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        commandBuffer->beginRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        {
            VkImageSubresourceRange subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT,
                level, 1,
                layer, 1
            };

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = dstImage.getHandle();
            barrier.subresourceRange = subresourceRange;

            commandBuffer->pipelineBarrier(barrier, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        }

        VkBufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = level;
        region.imageSubresource.baseArrayLayer = layer;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        VkExtent3D baseExtent = dstImage.getExtent();
        VkExtent3D mipExtent;
        mipExtent.width = std::max(1u, baseExtent.width >> level);
        mipExtent.height = std::max(1u, baseExtent.height >> level);
        mipExtent.depth = std::max(1u, baseExtent.depth >> level);
        region.imageExtent = mipExtent;
        commandBuffer->copyBufferToImage(stagingBuffer.getHandle(), dstImage.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, std::span<const VkBufferImageCopy>(&region, 1));

        {
            VkImageSubresourceRange subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT,
                level, 1,
                layer, 1
            };

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = dstImage.getHandle();
            barrier.subresourceRange = subresourceRange;

            commandBuffer->pipelineBarrier(barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }

        commandBuffer->endRecord();

        Queue& graphicsQueue = device.getGraphicsQueue();
        graphicsQueue.submit(*commandBuffer, {}, {}, {});
        graphicsQueue.waitIdle();
    }

    void Utils::uploadDataToImageAndGenMips(const Device& device, Image& dstImage, const void* data, VkDeviceSize size)
    {
        // Note:
        // 1.目前只支持颜色纹理;
        // 2.目前只支持数组层数为1的纹理;

        Buffer::CreateInfo bufferInfo(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        Buffer stagingBuffer(device, bufferInfo, VMA_MEMORY_USAGE_AUTO, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
        MemoryAllocation* allocation = stagingBuffer.getMemory();
        void* mappedData = allocation->map();
        std::memcpy(mappedData, data, size);
        allocation->unmap();

        CommandPool& commandPool = device.getGraphicsCommandPool();
        TRef<CommandBuffer> commandBuffer = commandPool.allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        commandBuffer->beginRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        {
            VkImageSubresourceRange subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0, dstImage.getMipLevels(),
                0, 1
            };

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = dstImage.getHandle();
            barrier.subresourceRange = subresourceRange;

            commandBuffer->pipelineBarrier(barrier, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
        }

        VkBufferImageCopy region;
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = { 0, 0, 0 };
        region.imageExtent = dstImage.getExtent();
        commandBuffer->copyBufferToImage(stagingBuffer.getHandle(), dstImage.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, std::span<const VkBufferImageCopy>(&region, 1));

        int32_t mipWidth = dstImage.getExtent().width;
        int32_t mipHeight = dstImage.getExtent().height;
        for (uint32_t i = 1; i < dstImage.getMipLevels(); ++i)
        {
            VkImageSubresourceRange mipSubresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT,
                i - 1, 1,
                0, 1
            };

            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = dstImage.getHandle();
            barrier.subresourceRange = mipSubresourceRange;

            commandBuffer->pipelineBarrier(barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkImageBlit blit{};
            blit.srcOffsets[0] = { 0, 0, 0 };
            blit.srcOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;

            mipWidth = std::max(mipWidth / 2, 1);
            mipHeight = std::max(mipHeight / 2, 1);
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { mipWidth, mipHeight, 1 };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            commandBuffer->blitImage(dstImage.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dstImage.getHandle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, std::span<const VkImageBlit>(&blit, 1), VK_FILTER_LINEAR);

            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            commandBuffer->pipelineBarrier(barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }

        {
            VkImageSubresourceRange subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT,
                dstImage.getMipLevels() - 1, 1,
                0, 1
            };
            VkImageMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = dstImage.getHandle();
            barrier.subresourceRange = subresourceRange;

            commandBuffer->pipelineBarrier(barrier, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }

        commandBuffer->endRecord();

        Queue& graphicsQueue = device.getGraphicsQueue();
        graphicsQueue.submit(*commandBuffer, {}, {}, {});
        graphicsQueue.waitIdle();
    }

    void Utils::transitionImageLayout(
        const Device& device,
        Image& image,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkAccessFlags srcAccessMask,
        VkAccessFlags dstAccessMask,
        const VkImageSubresourceRange& subresourceRange,
        VkPipelineStageFlags srcStageMask,
        VkPipelineStageFlags dstStageMask
    )
    {
        CommandPool& commandPool = device.getGraphicsCommandPool();
        TRef<CommandBuffer> commandBuffer = commandPool.allocateCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY);
        commandBuffer->beginRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccessMask;
        barrier.dstAccessMask = dstAccessMask;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image.getHandle();
        barrier.subresourceRange = subresourceRange;

        commandBuffer->pipelineBarrier(barrier, srcStageMask, dstStageMask);
        commandBuffer->endRecord();

        Queue& graphicsQueue = device.getGraphicsQueue();
        graphicsQueue.submit(*commandBuffer, {}, {}, {});
        graphicsQueue.waitIdle();
    }

    TRef<ShaderModule> Utils::createShaderModuleFromFile(
        const Device& device,
        const std::filesystem::path& path
    )
    {
        std::ifstream file(path, std::ios::ate | std::ios::binary);
        if (!file.is_open())
        {
            LOG_ERROR("Failed to open shader file: {}", path.string());
            return nullptr;
        }
        size_t fileSize = (size_t)file.tellg();
        assert(fileSize % sizeof(uint32_t) == 0);
        size_t spirvCodeCount = fileSize / sizeof(uint32_t);
        std::vector<uint32_t> spirvCode(spirvCodeCount);
        file.seekg(0);
        file.read((char*)spirvCode.data(), fileSize);
        file.close();

        return new ShaderModule(device, spirvCode);
    }

}
