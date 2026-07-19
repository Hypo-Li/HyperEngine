#include "GeometryProxy.h"
#include "RenderBackend.h"

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
        std::vector<uint64_t>&& vertexBindingOffsets,
        std::vector<uint8_t> indexData,
        rhi::IndexType indexType,
        std::vector<Section>&& sections
    ) : mId(acquireGeometryId()),
        mVertexLayout(vertexLayout),
        mVertexBindingOffsets(std::move(vertexBindingOffsets)),
        mIndexType(indexType),
        mSections(std::move(sections))
    {
        mIndexOffset = (vertexData.size() + 15) & ~15; // 对齐到16字节，满足vertex buffer和index buffer的对齐要求

        uint64_t totalSize = mIndexOffset + indexData.size();
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = totalSize;
        bufferDesc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::Index | rhi::BufferUsage::Transfer_Dst;
        mBuffer = getRenderBackend().getDevice().createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);

        mCachedBuffers.resize(mVertexBindingOffsets.size(), mBuffer.get());

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

    void GeometryProxy::prepare(rhi::CommandList& cmdList)
    {
        if (!mPendingUploads.empty())
        {
            flushUploads(cmdList);
        }
    }

    void GeometryProxy::bind(rhi::CommandList& cmdList) const noexcept
    {
        cmdList.bindVertexBuffers(0, mCachedBuffers, mVertexBindingOffsets);
        cmdList.bindIndexBuffer(mBuffer, mIndexOffset, mIndexType);
    }

    void GeometryProxy::draw(rhi::CommandList& cmdList, uint32_t sectionIdx, uint32_t instanceCount, uint32_t firstInstance) const noexcept
    {
        assert(sectionIdx < mSections.size());
        const auto& section = mSections[sectionIdx];
        cmdList.drawIndexed(section.indexCount, instanceCount, section.firstIndex, section.vertexOffset, firstInstance);
    }

    void GeometryProxy::flushUploads(rhi::CommandList& cmdList)
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

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mBuffer;
        preBarrier.offset = 0;
        preBarrier.size = rhi::kWholeBuffer;
        preBarrier.oldState = rhi::ResourceState::Vertex_Attachment | rhi::ResourceState::Index_Attachment;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        for (const auto& upload : mPendingUploads)
        {
            if (!upload.stagingAllocation)
                break;

            auto& allocation = upload.stagingAllocation.value();

            rhi::BufferCopyRegion region;
            region.srcOffset = allocation.offset;
            region.dstOffset = upload.offset;
            region.size = allocation.size;
            cmdList.copyBuffer(allocation.buffer, mBuffer, { &region, 1 });
        }

        mPendingUploads.erase(mPendingUploads.begin(), mPendingUploads.begin() + stagedCount);

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mBuffer;
        postBarrier.offset = 0;
        postBarrier.size = rhi::kWholeBuffer;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Vertex_Attachment | rhi::ResourceState::Index_Attachment;
        cmdList.insertBarriers({ &postBarrier, 1 });
    }

}
