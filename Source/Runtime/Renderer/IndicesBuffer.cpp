#include "IndicesBuffer.h"

#include <Core/Common/LogSystem.h>

#include <cstring>

namespace hyp
{
    IndicesBuffer::IndicesBuffer(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend)
    {
        rhi::BufferDesc bufferDesc;
        bufferDesc.size = kCapacity * sizeof(uint32_t);
        bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
        mBuffer = renderBackend.getDevice().createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
    }

    bool IndicesBuffer::upload(rhi::CommandList& cmdList, std::span<const uint32_t> indices)
    {
        if (indices.empty())
            return true;

        if (indices.size() > kCapacity)
        {
            LOG_ERROR("IndicesBuffer::upload: 数据量超出固定容量({} > {}), 这次上传被丢弃", indices.size(), kCapacity);
            return false;
        }

        const size_t byteSize = indices.size() * sizeof(uint32_t);
        auto allocation = mRenderBackend.getStagingAllocator().forceAllocate(byteSize);
        std::memcpy(allocation.mappedData, indices.data(), byteSize);

        rhi::BufferBarrier preBarrier;
        preBarrier.buffer = mBuffer.get();
        preBarrier.offset = 0;
        preBarrier.size = byteSize;
        preBarrier.oldState = rhi::ResourceState::Shader_Storage_Read;
        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
        cmdList.insertBarriers({ &preBarrier, 1 });

        rhi::BufferCopyRegion copyRegion;
        copyRegion.srcOffset = allocation.offset;
        copyRegion.dstOffset = 0;
        copyRegion.size = byteSize;
        cmdList.copyBuffer(allocation.buffer, mBuffer.get(), { &copyRegion, 1 });

        rhi::BufferBarrier postBarrier;
        postBarrier.buffer = mBuffer.get();
        postBarrier.offset = 0;
        postBarrier.size = byteSize;
        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
        postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
        cmdList.insertBarriers({ &postBarrier, 1 });

        return true;
    }
}
