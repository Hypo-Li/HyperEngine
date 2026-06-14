#include "TextureProxy.h"
#include "RenderBackend.h"

namespace hyp
{
    TextureProxy::TextureProxy(const vk::Image::CreateInfo& imageInfo)
    {
        auto& renderBackend = getRenderBackend();
        mImage = new vk::Image(renderBackend.getDevice(), imageInfo, VMA_MEMORY_USAGE_GPU_ONLY);
        mImageView = mImage->createView(vk::Image::ViewInfo(*mImage));
        mInitialized = false;

        mBindlessHandle = renderBackend.getBindlessRegistry().registerTexture2D(mImageView->getHandle());
    }

    TextureProxy::~TextureProxy() noexcept
    {
        auto& bindlessRegistry = getRenderBackend().getBindlessRegistry();

        getRenderBackend().getDeferredTaskQueue().push(
            [image = std::move(mImage),
            imageView = std::move(mImageView),
            bindlessRegistry,
            bindlessHandle = std::move(mBindlessHandle)]() mutable
            {
                bindlessRegistry.unregister(bindlessHandle);
                image = nullptr;
                imageView = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void TextureProxy::stageUpload(std::vector<uint8_t> data, uint32_t mipLevel, uint32_t baseLayer, uint32_t layerCount)
    {
        PendingUpload& pendingUpload = mPendingUploads.emplace_back();
        pendingUpload.mipLevel = mipLevel;
        pendingUpload.baseLayer = baseLayer;
        pendingUpload.layerCount = layerCount;
        pendingUpload.data = std::move(data);
    }

    void TextureProxy::prepare(vk::CommandBuffer& cmd)
    {
        if (!mInitialized)
        {
            VkImageSubresourceRange subresourceRange = {
                VK_IMAGE_ASPECT_COLOR_BIT,
                0, mImage->getMipLevels(),
                0, mImage->getArrayLayers()
            };
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_NONE;
            barrier.srcAccessMask = VK_ACCESS_2_NONE;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
            barrier.dstAccessMask = VK_ACCESS_2_NONE;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = mImage->getHandle();
            barrier.subresourceRange = subresourceRange;

            cmd.pipelineBarrier2({ &barrier, 1 });

            mInitialized = true;
        }

        if (!mPendingUploads.empty())
        {
            flushUploads(cmd);
        }
    }

    void TextureProxy::flushUploads(vk::CommandBuffer& cmd)
    {
        assert(!mPendingUploads.empty());

        auto& stagingAllocator = getRenderBackend().getStagingAllocator();
        size_t stagedCount = 0;
        for (auto& upload : mPendingUploads)
        {
            upload.stagingAllocation = stagingAllocator.tryAllocate(upload.data.size());
            if (!upload.stagingAllocation)
            {
                // 本帧内可用Staging Buffer已耗尽, 等待下一帧再上传
                break;
            }

            std::memcpy(upload.stagingAllocation->mappedData, upload.data.data(), upload.data.size());
            upload.data.clear();
            upload.data.shrink_to_fit();
            stagedCount++;
        }

        if (stagedCount == 0)
            return;

        VkPipelineStageFlags2 stageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        VkAccessFlags2 accessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;

        VkImageSubresourceRange subresourceRange = {
            VK_IMAGE_ASPECT_COLOR_BIT,
            0, mImage->getMipLevels(),
            0, mImage->getArrayLayers()
        };
        {
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = stageMask;
            barrier.srcAccessMask = accessMask;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = mImage->getHandle();
            barrier.subresourceRange = subresourceRange;

            cmd.pipelineBarrier2({ &barrier, 1 });
        }

        for (const auto& upload : mPendingUploads)
        {
            if (!upload.stagingAllocation)
                break;

            auto& allocation = upload.stagingAllocation.value();

            VkBufferImageCopy region{};
            region.bufferOffset = allocation.offset;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = upload.mipLevel;
            region.imageSubresource.baseArrayLayer = upload.baseLayer;
            region.imageSubresource.layerCount = upload.layerCount;
            region.imageOffset = { 0, 0, 0 };
            VkExtent3D baseExtent = mImage->getExtent();
            region.imageExtent.width = std::max(1u, baseExtent.width >> upload.mipLevel);
            region.imageExtent.height = std::max(1u, baseExtent.height >> upload.mipLevel);
            region.imageExtent.depth = std::max(1u, baseExtent.depth >> upload.mipLevel);

            cmd.copyBufferToImage(
                allocation.buffer,
                mImage->getHandle(),
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                std::span(&region, 1)
            );

        }

        mPendingUploads.erase(mPendingUploads.begin(), mPendingUploads.begin() + stagedCount);

        {
            VkImageMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = stageMask;
            barrier.dstAccessMask = accessMask;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = mImage->getHandle();
            barrier.subresourceRange = subresourceRange;

            cmd.pipelineBarrier2({ &barrier, 1 });
        }
    }

}
