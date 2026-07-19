#include "CommandList.h"

#include <Core/Common/LogSystem.h>

namespace hyp::rhi
{
    CommandList::CommandList(const Device& device, QueueType queueType) :
        mDevice(device), mQueueType(queueType)
    {
#ifndef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        mStorage.reserve(4096);
#endif
    }

    void CommandList::reset()
    {
#ifndef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        mStorage.clear();
        mNativeCallbacks.clear();
#endif
    }

    void CommandList::bindPipeline(const Pipeline* pipeline)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        bindPipelineBackend(pipeline);
#else
        pushCommand<BindPipelineCommand>(CommandType::Bind_Pipeline, pipeline);
#endif
    }

    void CommandList::setBindGroup(uint32_t index, PipelineBindPoint bindPoint, const PipelineLayout* pipelineLayout, const BindGroup* bindGroup, std::span<const uint32_t> dynamicOffsets)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        setBindGroupBackend(index, bindPoint, pipelineLayout, bindGroup, dynamicOffsets);
#else
        pushCommand1Trailing<SetBindGroupCommand>(
            CommandType::Set_Bind_Group,
            dynamicOffsets,
            index, bindPoint, pipelineLayout, bindGroup, static_cast<uint32_t>(dynamicOffsets.size())
        );
#endif
    }

    void CommandList::bindVertexBuffers(uint32_t firstBinding, std::span<const Buffer* const> buffers, std::span<const uint64_t> offsets)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        bindVertexBuffersBackend(firstBinding, buffers, offsets);
#else
        assert(buffers.size() == offsets.size());
        pushCommand2Trailing<BindVertexBuffersCommand>(
            CommandType::Bind_Vertex_Buffers,
            buffers,
            offsets,
            firstBinding, static_cast<uint32_t>(buffers.size())
        );
#endif
    }

    void CommandList::bindIndexBuffer(const Buffer* buffer, uint64_t offset, IndexType indexType)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        bindIndexBufferBackend(buffer, offset, indexType);
#else
        pushCommand<BindIndexBufferCommand>(CommandType::Bind_Index_Buffer, buffer, offset, indexType);
#endif
    }

    void CommandList::setViewport(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        setViewportBackend(x, y, width, height, minDepth, maxDepth);
#else
        pushCommand<SetViewportCommand>(CommandType::Set_Viewport, x, y, width, height, minDepth, maxDepth);
#endif
    }

    void CommandList::setScissor(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        setScissorBackend(x, y, width, height);
#else
        pushCommand<SetScissorCommand>(CommandType::Set_Scissor, x, y, width, height);
#endif
    }

    void CommandList::setBlendConstants(float4 blendConstants)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        setBlendConstantsBackend(blendConstants);
#else
        pushCommand<SetBlendConstantsCommand>(CommandType::Set_Blend_Constants, blendConstants);
#endif
    }

    void CommandList::setStencilReference(uint32_t reference)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        setStencilReferenceBackend(reference);
#else
        pushCommand<SetStencilReferenceCommand>(CommandType::Set_Stencil_Reference, reference);
#endif
    }

    void CommandList::setImmediateData(const PipelineLayout* pipelineLayout, uint32_t offset, std::span<const uint8_t> data)
    {
        size_t copySize = data.size();
        if (offset >= kImmediateDataSize)
        {
            LOG_WARN("setImmediateData: offset {} is out of the {}-byte range, ignoring", offset, kImmediateDataSize);
            copySize = 0;
        }
        else if (offset + copySize > kImmediateDataSize)
        {
            LOG_WARN("setImmediateData: offset {} + data size {} exceeds the {}-byte limit, truncating", offset, data.size(), kImmediateDataSize);
            copySize = kImmediateDataSize - offset;
        }
        std::span<const uint8_t> clampedData = data.subspan(0, copySize);

#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        setImmediateDataBackend(pipelineLayout, offset, clampedData);
#else
        pushCommand1Trailing<SetImmediateDataCommand>(
            CommandType::Set_Immediate_Data,
            clampedData,
            pipelineLayout, offset, static_cast<uint32_t>(copySize)
        );
#endif
    }

    void CommandList::beginRenderPass(RenderArea renderArea, const DepthStencilAttachmentInfo& depthStencilAttachment, std::span<const ColorAttachmentInfo> colorAttachments)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        beginRenderPassBackend(renderArea, depthStencilAttachment, colorAttachments);
#else
        pushCommand1Trailing<BeginRenderPassCommand>(
            CommandType::Begin_Render_Pass,
            colorAttachments,
            renderArea, depthStencilAttachment,
            static_cast<uint32_t>(colorAttachments.size())
        );
#endif
    }

    void CommandList::endRenderPass()
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        endRenderPassBackend();
#else
        pushCommand<EndRenderPassCommand>(CommandType::End_Render_Pass);
#endif
    }

    void CommandList::beginComputePass()
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        beginComputePassBackend();
#else
        pushCommand<BeginComputePassCommand>(CommandType::Begin_Compute_Pass);
#endif
    }

    void CommandList::endComputePass()
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        endComputePassBackend();
#else
        pushCommand<EndComputePassCommand>(CommandType::End_Compute_Pass);
#endif
    }

    void CommandList::draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        drawBackend(vertexCount, instanceCount, firstVertex, firstInstance);
#else
        pushCommand<DrawCommand>(CommandType::Draw, vertexCount, instanceCount, firstVertex, firstInstance);
#endif
    }

    void CommandList::drawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        drawIndexedBackend(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
#else
        pushCommand<DrawIndexedCommand>(CommandType::Draw_Indexed, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
#endif
    }

    void CommandList::drawIndirect(const Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        drawIndirectBackend(buffer, offset, drawCount, stride);
#else
        pushCommand<DrawIndirectCommand>(CommandType::Draw_Indirect, buffer, offset, drawCount, stride);
#endif
    }

    void CommandList::drawIndexedIndirect(const Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        drawIndexedIndirectBackend(buffer, offset, drawCount, stride);
#else
        pushCommand<DrawIndexedIndirectCommand>(CommandType::Draw_Indexed_Indirect, buffer, offset, drawCount, stride);
#endif
    }

    void CommandList::dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        dispatchBackend(groupCountX, groupCountY, groupCountZ);
#else
        pushCommand<DispatchCommand>(CommandType::Dispatch, groupCountX, groupCountY, groupCountZ);
#endif
    }

    void CommandList::dispatchIndirect(const Buffer* buffer, uint64_t offset)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        dispatchIndirectBackend(buffer, offset);
#else
        pushCommand<DispatchIndirectCommand>(CommandType::Dispatch_Indirect, buffer, offset);
#endif
    }

    void CommandList::insertBarriers(std::span<const BufferBarrier> bufferBarriers, std::span<const TextureBarrier> textureBarriers)
    {
        if (bufferBarriers.empty() && textureBarriers.empty())
            return;

#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        insertBarriersBackend(bufferBarriers, textureBarriers);
#else
        pushCommand2Trailing<BarrierCommand>(
            CommandType::Barrier,
            bufferBarriers,
            textureBarriers,
            static_cast<uint32_t>(bufferBarriers.size()), static_cast<uint32_t>(textureBarriers.size())
        );
#endif
    }

    void CommandList::copyBuffer(const Buffer* srcBuffer, const Buffer* dstBuffer, std::span<const BufferCopyRegion> regions)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        copyBufferBackend(srcBuffer, dstBuffer, regions);
#else
        pushCommand1Trailing<CopyBufferCommand>(
            CommandType::Copy_Buffer,
            regions,
            srcBuffer, dstBuffer, static_cast<uint32_t>(regions.size())
        );
#endif
    }

    void CommandList::copyBufferToTexture(const Buffer* srcBuffer, const Texture* dstTexture, std::span<const BufferTextureCopyRegion> regions)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        copyBufferToTextureBackend(srcBuffer, dstTexture, regions);
#else
        pushCommand1Trailing<CopyBufferToTextureCommand>(
            CommandType::Copy_Buffer_To_Texture,
            regions,
            srcBuffer, dstTexture, static_cast<uint32_t>(regions.size())
        );
#endif
    }

    void CommandList::clearBuffer(const Buffer* dstBuffer, uint64_t dstOffset, uint64_t size)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        clearBufferBackend(dstBuffer, dstOffset, size);
#else
        pushCommand<ClearBufferCommand>(CommandType::Clear_Buffer, dstBuffer, dstOffset, size);
#endif
    }

    void CommandList::copyTexture(const Texture* srcTexture, const Texture* dstTexture, std::span<const TextureCopyRegion> regions)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        copyTextureBackend(srcTexture, dstTexture, regions);
#else
        pushCommand1Trailing<CopyTextureCommand>(
            CommandType::Copy_Texture,
            regions,
            srcTexture, dstTexture, static_cast<uint32_t>(regions.size())
        );
#endif
    }

    void CommandList::copyTextureToBuffer(const Texture* srcTexture, const Buffer* dstBuffer, std::span<const BufferTextureCopyRegion> regions)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        copyTextureToBufferBackend(srcTexture, dstBuffer, regions);
#else
        pushCommand1Trailing<CopyTextureToBufferCommand>(
            CommandType::Copy_Texture_To_Buffer,
            regions,
            srcTexture, dstBuffer, static_cast<uint32_t>(regions.size())
        );
#endif
    }

    void CommandList::beginDebugGroup(HString label, float4 color)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        beginDebugGroupBackend(label, color);
#else
        pushCommand<BeginDebugGroupCommand>(CommandType::Begin_Debug_Group, label, color);
#endif
    }

    void CommandList::endDebugGroup()
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        endDebugGroupBackend();
#else
        pushCommand<EndDebugGroupCommand>(CommandType::End_Debug_Group);
#endif
    }

    void CommandList::insertDebugMarker(HString label, float4 color)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        insertDebugMarkerBackend(label, color);
#else
        pushCommand<InsertDebugMarkerCommand>(CommandType::Insert_Debug_Marker, label, color);
#endif
    }

    void CommandList::invokeNativeCallback(NativeRenderCallback callback)
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        // 立即模式下当场同步调用, 不需要保活到 decode() —— 直接用调用方传进来的这份, 不用
        // 走 mNativeCallbacks。
        invokeNativeCallbackBackend(callback);
#else
        // 移进 mNativeCallbacks(deque, 不会因为扩容搬移已有元素), 指令流里只存指向它的指针 ——
        // 见 NativeCallbackCommand/mNativeCallbacks 的注释。
        mNativeCallbacks.push_back(std::move(callback));
        const NativeRenderCallback* ptr = &mNativeCallbacks.back();
        pushCommand<NativeCallbackCommand>(CommandType::Native_Callback, ptr);
#endif
    }

    const uint8_t* CommandList::alignPointer(const uint8_t* ptr, size_t alignment) noexcept
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t aligned = (addr + (alignment - 1)) & ~(alignment - 1);
        return reinterpret_cast<const uint8_t*>(aligned);
    }

    uint8_t* CommandList::allocateCommand(size_t size, size_t alignment)
    {
        alignment = std::max(alignment, size_t(8));
        size_t oldSize = mStorage.size();
        size_t alignedOffset = alignUp(oldSize, alignment);
        size_t padding = alignedOffset - oldSize;
        size_t totalSize = padding + size;
        mStorage.resize(oldSize + totalSize);
        return mStorage.data() + alignedOffset;
    }
}
