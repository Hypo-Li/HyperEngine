#include "GeometryProxy.h"
#include "RenderBackend.h"
#include "Vulkan/Queue.h"

namespace hyp
{
    uint32_t gGeometryId = 0;
    std::vector<uint32_t> gFreeGeometryIds;

    // 临时实现, 后续需要建立统一的几何体管理器
    uint32_t acquireGeometryId()
    {
        if (gFreeGeometryIds.empty())
        {
            return gGeometryId++;
        }
        else
        {
            auto id = gFreeGeometryIds.back();
            gFreeGeometryIds.pop_back();
            return id;
        }
    }

    void releaseGeometryId(uint32_t id)
    {
        gFreeGeometryIds.push_back(id);
    }

    GeometryProxy::GeometryProxy(
        VertexLayout vertexLayout,
        std::vector<uint8_t> vertexData,
        std::vector<VkDeviceSize>&& vertexBindingOffsets,
        std::vector<uint8_t> indexData,
        VkIndexType indexType,
        std::vector<Section>&& sections
    ) : mId(acquireGeometryId()),
        mVertexLayout(vertexLayout),
        mVertexBindingOffsets(std::move(vertexBindingOffsets)),
        mIndexType(indexType),
        mSections(std::move(sections))
    {
        mIndexOffset = (vertexData.size() + 15) & ~15; // 对齐到16字节，满足vertex buffer和index buffer的对齐要求

        VkDeviceSize totalSize = mIndexOffset + indexData.size();
        vk::Buffer::CreateInfo bufferInfo;
        bufferInfo.size = totalSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        mBuffer = new vk::Buffer(getRenderBackend().getDevice(), bufferInfo, VMA_MEMORY_USAGE_GPU_ONLY);

        mCachedBufferHandles.resize(mVertexBindingOffsets.size(), mBuffer->getHandle());

        mPendingUploads.emplace_back(std::move(vertexData), 0);
        mPendingUploads.emplace_back(std::move(indexData), mIndexOffset);
    }

    GeometryProxy::~GeometryProxy() noexcept
    {
        getRenderBackend().getDeferredTaskQueue().push(
            [buffer = std::move(mBuffer)]() mutable
            {
                buffer = nullptr;
            },
            kMaxFramesInFlight
        );
        releaseGeometryId(mId);
    }

    void GeometryProxy::prepare(vk::CommandBuffer& cmd)
    {
        if (!mPendingUploads.empty())
        {
            flushUploads(cmd);
        }
    }

    void GeometryProxy::bind(vk::CommandBuffer& cmd) const noexcept
    {
        cmd.bindVertexBuffers(0, mCachedBufferHandles, mVertexBindingOffsets);
        cmd.bindIndexBuffer(mBuffer->getHandle(), mIndexOffset, mIndexType);
    }

    void GeometryProxy::draw(vk::CommandBuffer& cmd, uint32_t sectionIdx, uint32_t instanceCount, uint32_t firstInstance) const noexcept
    {
        assert(sectionIdx < mSections.size());
        const auto& section = mSections[sectionIdx];
        cmd.drawIndexed(section.indexCount, instanceCount, section.firstIndex, section.vertexOffset, firstInstance);
    }

    void GeometryProxy::flushUploads(vk::CommandBuffer& cmd)
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

        {
            VkBufferMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = mBuffer->getHandle();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            cmd.pipelineBarrier2({ &barrier, 1 });
        }

        for (const auto& upload : mPendingUploads)
        {
            if (!upload.stagingAllocation)
                break;

            auto& allocation = upload.stagingAllocation.value();

            VkBufferCopy copyRegion{};
            copyRegion.srcOffset = allocation.offset;
            copyRegion.dstOffset = upload.offset;
            copyRegion.size = allocation.size;
            cmd.copyBuffer(allocation.buffer, mBuffer->getHandle(), std::span(&copyRegion, 1));
        }

        mPendingUploads.erase(mPendingUploads.begin(), mPendingUploads.begin() + stagedCount);

        {
            VkBufferMemoryBarrier2 barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
            barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
            barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT;
            barrier.dstAccessMask = VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer = mBuffer->getHandle();
            barrier.offset = 0;
            barrier.size = VK_WHOLE_SIZE;
            cmd.pipelineBarrier2({ &barrier, 1 });
        }
    }

}
