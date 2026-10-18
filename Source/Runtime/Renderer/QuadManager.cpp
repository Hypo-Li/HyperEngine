#include "QuadManager.h"
#include <RenderBase/RenderBackend.h>

#include <algorithm>
#include <cassert>
#include <cstring>

namespace hyp
{
    VertexLayout QuadManager::getVertexLayout()
    {
        const VertexAttributeMask mask =
            VertexAttributeMask::Position | VertexAttributeMask::Normal | VertexAttributeMask::Tangent |
            VertexAttributeMask::Color | VertexAttributeMask::TexCoord0 | VertexAttributeMask::TexCoord1 |
            VertexAttributeMask::TexCoord2 | VertexAttributeMask::TexCoord3;
        return VertexLayout(VertexLayoutType::Quad, mask);
    }

    QuadManager::QuadManager(RenderBackend& renderBackend, uint32_t elementCapacity, uint32_t instanceCapacity)
        : mRenderBackend(renderBackend)
        , mElementTable(renderBackend, elementCapacity, "QuadManager.ElementBuffer")
        , mInstanceCapacity(instanceCapacity)
    {
        auto& device = renderBackend.getDevice();

        rhi::BufferDesc instanceBufferDesc;
        instanceBufferDesc.size = static_cast<uint64_t>(instanceCapacity) * sizeof(QuadInstanceData);
        instanceBufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
        mInstanceDataBuffer = device.createBuffer(instanceBufferDesc, rhi::MemoryType::Gpu_Only);
        mInstanceDataBuffer->setDebugName("QuadManager.InstanceDataBuffer");
    }

    QuadManager::~QuadManager()
    {
        // 场景可能在运行中途被销毁, GPU 上可能还有飞行中的渲染指令在读这块 buffer,
        // 延迟 kMaxFramesInFlight 帧再真正销毁, 照抄 SkinningManager::~SkinningManager()
        mRenderBackend.getDeferredTaskQueue().push(
            [instanceDataBuffer = std::move(mInstanceDataBuffer)]() mutable
            {
                instanceDataBuffer = nullptr;
            },
            kMaxFramesInFlight
        );
    }

    std::optional<uint32_t> QuadManager::allocateElementRange(uint32_t elementCount)
    {
        return mElementTable.allocate(elementCount);
    }

    void QuadManager::freeElementRange(uint32_t offset, uint32_t elementCount) noexcept
    {
        mElementTable.free(offset, elementCount);
    }

    void QuadManager::queueElementUpload(uint32_t offset, std::vector<QuadElement>&& elements)
    {
        mElementTable.queueUpload(offset, std::move(elements));
    }

    std::optional<uint32_t> QuadManager::acquireInstanceIndex()
    {
        if (!mFreeInstanceIndices.empty())
        {
            uint32_t instanceIndex = mFreeInstanceIndices.back();
            mFreeInstanceIndices.pop_back();
            return instanceIndex;
        }

        if (mInstanceIndexCount >= mInstanceCapacity)
            return std::nullopt;

        return mInstanceIndexCount++;
    }

    void QuadManager::releaseInstanceIndex(uint32_t instanceIndex) noexcept
    {
        std::erase_if(mPendingInstanceUploads, [instanceIndex](const PendingInstanceUpload& upload) { return upload.instanceIndex == instanceIndex; });
        mFreeInstanceIndices.push_back(instanceIndex);
    }

    void QuadManager::setInstanceData(uint32_t instanceIndex, const QuadInstanceData& data)
    {
        assert(instanceIndex < mInstanceIndexCount);

        auto existing = std::find_if(mPendingInstanceUploads.begin(), mPendingInstanceUploads.end(),
            [instanceIndex](const PendingInstanceUpload& upload) { return upload.instanceIndex == instanceIndex; });
        if (existing != mPendingInstanceUploads.end())
        {
            existing->data = data;
            return;
        }

        mPendingInstanceUploads.push_back(PendingInstanceUpload{ instanceIndex, data, std::nullopt });
    }

    void QuadManager::prepare(rhi::CommandList& cmdList)
    {
        mElementTable.prepare(cmdList);

        // 整块清零: 还没写过的条目 elementCount 为 0, 顶点着色器会把它们折叠成退化三角形
        if (!mInstanceDataBufferInitialized)
        {
            rhi::BufferBarrier preBarrier;
            preBarrier.buffer = mInstanceDataBuffer.get();
            preBarrier.offset = 0;
            preBarrier.size = rhi::kWholeBuffer;
            preBarrier.oldState = rhi::ResourceState::None;
            preBarrier.newState = rhi::ResourceState::Transfer_Dst;
            cmdList.insertBarriers({ &preBarrier, 1 });

            cmdList.clearBuffer(mInstanceDataBuffer.get(), 0, mInstanceDataBuffer->getDesc().size);

            rhi::BufferBarrier postBarrier;
            postBarrier.buffer = mInstanceDataBuffer.get();
            postBarrier.offset = 0;
            postBarrier.size = rhi::kWholeBuffer;
            postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
            postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
            cmdList.insertBarriers({ &postBarrier, 1 });

            mInstanceDataBufferInitialized = true;
        }

        if (!mPendingInstanceUploads.empty())
            flushInstanceUploads(cmdList);
    }

    void QuadManager::flushInstanceUploads(rhi::CommandList& cmdList)
    {
        assert(!mPendingInstanceUploads.empty());

        // 元素还有没传完的, 实例条目就不能生效, 否则会指向还没就位的元素
        if (mElementTable.hasPendingUploads())
            return;

        auto& stagingAllocator = mRenderBackend.getStagingAllocator();
        size_t stagedCount = 0;
        for (auto& upload : mPendingInstanceUploads)
        {
            upload.stagingAllocation = stagingAllocator.tryAllocate(sizeof(QuadInstanceData));
            if (!upload.stagingAllocation)
                break; // 本帧内可用 staging buffer 已耗尽, 等待下一帧再上传

            std::memcpy(upload.stagingAllocation->mappedData, &upload.data, sizeof(QuadInstanceData));
            stagedCount++;
        }

        if (stagedCount == 0)
            return;

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mInstanceDataBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = rhi::kWholeBuffer;
        preBarrier.oldState = rhi::ResourceState::Shader_Storage_Read;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        for (size_t i = 0; i < stagedCount; ++i)
        {
            auto& upload = mPendingInstanceUploads[i];
            auto& allocation = upload.stagingAllocation.value();

            rhi::BufferCopyRegion region;
            region.srcOffset = allocation.offset;
            region.dstOffset = static_cast<uint64_t>(upload.instanceIndex) * sizeof(QuadInstanceData);
            region.size = allocation.size;
            cmdList.copyBuffer(allocation.buffer, mInstanceDataBuffer.get(), { &region, 1 });
        }

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mInstanceDataBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = rhi::kWholeBuffer;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
        cmdList.insertBarriers({ &postBarrier, 1 });

        mPendingInstanceUploads.erase(mPendingInstanceUploads.begin(), mPendingInstanceUploads.begin() + stagedCount);
    }
}
