#include "RangeTable.h"
#include "RenderBackend.h"

#include <cassert>
#include <cstring>

namespace hyp
{
    RangeTableBase::RangeTableBase(RenderBackend& renderBackend, uint32_t capacity, uint32_t elementSize, HString debugName) :
        mRenderBackend(renderBackend),
        mElementSize(elementSize),
        mAllocator(capacity)
    {
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = static_cast<uint64_t>(capacity) * elementSize;
        bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
        mBuffer = renderBackend.getDevice().createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
        mBuffer->setDebugName(debugName);
    }

    RangeTableBase::~RangeTableBase()
    {
        // 飞行中的渲染指令可能还在读这块缓冲
        mRenderBackend.getDeferredTaskQueue().push(
            [buffer = std::move(mBuffer)]() mutable
            {
                buffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    void RangeTableBase::prepareBuffer(rhi::CommandList& cmdList)
    {
        if (mInitialized)
            return;

        rhi::BufferBarrier barrier;
        barrier.buffer = mBuffer.get();
        barrier.offset = 0;
        barrier.size = rhi::kWholeBuffer;
        barrier.oldState = rhi::ResourceState::None;
        barrier.newState = rhi::ResourceState::Shader_Storage_Read;
        cmdList.insertBarriers({ &barrier, 1 });

        mInitialized = true;
    }

    size_t RangeTableBase::flushUploads(rhi::CommandList& cmdList, std::span<const UploadView> uploads)
    {
        auto& stagingAllocator = mRenderBackend.getStagingAllocator();
        std::vector<StagingAllocation> allocations;
        allocations.reserve(uploads.size());
        for (const UploadView& upload : uploads)
        {
            std::optional<StagingAllocation> allocation = stagingAllocator.tryAllocate(upload.byteSize);
            if (!allocation)
                break; // 本帧内可用 staging buffer 已耗尽, 等待下一帧再上传

            std::memcpy(allocation->mappedData, upload.data, upload.byteSize);
            allocations.push_back(*allocation);
        }

        if (allocations.empty())
            return 0;

        // 全表只有这一个 buffer, 不管这一批有多少段更新, 只需要一组 pre/post barrier 覆盖整个 buffer
        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = rhi::kWholeBuffer;
        preBarrier.oldState = rhi::ResourceState::Shader_Storage_Read;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        for (size_t i = 0; i < allocations.size(); ++i)
        {
            rhi::BufferCopyRegion region;
            region.srcOffset = allocations[i].offset;
            region.dstOffset = static_cast<uint64_t>(uploads[i].offset) * mElementSize;
            region.size = allocations[i].size;
            cmdList.copyBuffer(allocations[i].buffer, mBuffer.get(), { &region, 1 });
        }

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = rhi::kWholeBuffer;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
        cmdList.insertBarriers({ &postBarrier, 1 });

        return allocations.size();
    }
}
