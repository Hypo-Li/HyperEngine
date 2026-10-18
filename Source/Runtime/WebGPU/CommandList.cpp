#include "CommandList.h"

#include "BindGroup.h"
#include "Buffer.h"
#include "Pipeline.h"
#include "Texture.h"
#include "WgpuFormatUtil.h"

#include <Core/Common/LogSystem.h>

#include <cassert>
#include <cstring>
#include <span>
#include <vector>

namespace hyp::wgpu
{
    static WGPUIndexFormat convertIndexType(rhi::IndexType indexType) noexcept
    {
        switch (indexType)
        {
        case rhi::IndexType::Uint16: return WGPUIndexFormat_Uint16;
        case rhi::IndexType::Uint32: return WGPUIndexFormat_Uint32;
        default:                     return WGPUIndexFormat_Undefined;
        }
    }

    static WGPULoadOp convertLoadOp(rhi::AttachmentLoadOp op) noexcept
    {
        switch (op)
        {
        case rhi::AttachmentLoadOp::Load:      return WGPULoadOp_Load;
        case rhi::AttachmentLoadOp::Clear:     return WGPULoadOp_Clear;
        case rhi::AttachmentLoadOp::Dont_Care: return WGPULoadOp_Clear;
        default:                               return WGPULoadOp_Undefined;
        }
    }

    static WGPUStoreOp convertStoreOp(rhi::AttachmentStoreOp op) noexcept
    {
        switch (op)
        {
        case rhi::AttachmentStoreOp::Store:     return WGPUStoreOp_Store;
        case rhi::AttachmentStoreOp::Dont_Care: return WGPUStoreOp_Discard;
        case rhi::AttachmentStoreOp::None:      return WGPUStoreOp_Undefined;
        default:                                return WGPUStoreOp_Store;
        }
    }

    static WGPUColor convertClearColor(const rhi::ClearColorValue& value) noexcept
    {
        WGPUColor color = {};
        color.r = value.float32.x;
        color.g = value.float32.y;
        color.b = value.float32.z;
        color.a = value.float32.w;
        return color;
    }

    static const uint8_t* alignPointer(const uint8_t* ptr, size_t alignment) noexcept
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
        uintptr_t aligned = (addr + (alignment - 1)) & ~(alignment - 1);
        return reinterpret_cast<const uint8_t*>(aligned);
    }

    static void setBindGroupOnEncoder(
        WGPURenderPassEncoder renderPassEncoder,
        WGPUComputePassEncoder computePassEncoder,
        rhi::PipelineBindPoint bindPoint,
        uint32_t index,
        const BindGroup* bindGroup,
        uint32_t dynamicOffsetCount,
        const uint32_t* dynamicOffsets
    )
    {
        WGPUBindGroup handle = bindGroup ? bindGroup->getHandle() : nullptr;
        if (bindPoint == rhi::PipelineBindPoint::Graphics)
        {
            assert(renderPassEncoder != nullptr && "SetBindGroup(Graphics) must be inside render pass");
            wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, index, handle, dynamicOffsetCount, dynamicOffsets);
        }
        else
        {
            assert(computePassEncoder != nullptr && "SetBindGroup(Compute) must be inside compute pass");
            wgpuComputePassEncoderSetBindGroup(computePassEncoder, index, handle, dynamicOffsetCount, dynamicOffsets);
        }
    }

    CommandList::CommandList(const Device& device, rhi::QueueType queueType)
        : rhi::CommandList(device, queueType)
    {
    }

    void CommandList::ensureEncoder()
    {
        if (mEncoder == nullptr)
        {
            WGPUCommandEncoderDescriptor encoderDesc = {};
            encoderDesc.nextInChain = nullptr;
            encoderDesc.label = WGPUStringView{ "UnnamedCommandEncoder", WGPU_STRLEN };
            mEncoder = wgpuDeviceCreateCommandEncoder(getWgpuDevice().getHandle(), &encoderDesc);
        }
    }

    void CommandList::endCurrentPass()
    {
        if (mRenderPassEncoder != nullptr)
        {
            wgpuRenderPassEncoderEnd(mRenderPassEncoder);
            wgpuRenderPassEncoderRelease(mRenderPassEncoder);
            mRenderPassEncoder = nullptr;
        }
        if (mComputePassEncoder != nullptr)
        {
            wgpuComputePassEncoderEnd(mComputePassEncoder);
            wgpuComputePassEncoderRelease(mComputePassEncoder);
            mComputePassEncoder = nullptr;
        }
    }

    void CommandList::releaseEncoder()
    {
        if (mEncoder != nullptr)
        {
            wgpuCommandEncoderRelease(mEncoder);
            mEncoder = nullptr;
        }
    }

    void CommandList::resetState()
    {
        endCurrentPass();
        releaseEncoder();
        for (auto& pending : mPendingGraphicsBindGroups)
        {
            pending.bindGroup = nullptr;
            pending.dynamicOffsets.clear();
        }
        for (auto& pending : mPendingComputeBindGroups)
        {
            pending.bindGroup = nullptr;
            pending.dynamicOffsets.clear();
        }
    }

    void CommandList::reset()
    {
        rhi::CommandList::reset();
        resetState();
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        ensureEncoder();
#endif
    }

    WGPUCommandBuffer CommandList::decode()
    {
#ifdef HYP_RHI_COMMAND_RECORDING_IMMEDIATE
        endCurrentPass();

        WGPUCommandBuffer commandBuffer = nullptr;
        if (mEncoder != nullptr)
        {
            WGPUCommandBufferDescriptor commandBufferDesc = {};
            commandBufferDesc.nextInChain = nullptr;
            commandBufferDesc.label = WGPUStringView{ "UnnamedCommandBuffer", WGPU_STRLEN };
            commandBuffer = wgpuCommandEncoderFinish(mEncoder, &commandBufferDesc);
            releaseEncoder();
        }
        return commandBuffer;
#else
        ensureEncoder();
        mRenderPassEncoder = nullptr;
        mComputePassEncoder = nullptr;

        std::span<const uint8_t> storage = getStorage();
        const uint8_t* ptr = storage.data();
        const uint8_t* end = ptr + storage.size();

        while (ptr < end)
        {
            ptr = alignPointer(ptr, 8);
            if (ptr >= end)
                break;

            const auto* header = reinterpret_cast<const rhi::CommandHeader*>(ptr);
            switch (header->type)
            {
            case rhi::CommandType::Bind_Pipeline:
            {
                const auto* cmd = reinterpret_cast<const rhi::BindPipelineCommand*>(ptr);
                bindPipelineBackend(cmd->pipeline);
                ptr += sizeof(rhi::BindPipelineCommand);
                break;
            }
            case rhi::CommandType::Set_Bind_Group:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetBindGroupCommand*>(ptr);
                const uint8_t* trailing = alignPointer(ptr + sizeof(rhi::SetBindGroupCommand), alignof(uint32_t));
                const uint32_t* dynamicOffsets = reinterpret_cast<const uint32_t*>(trailing);
                setBindGroupBackend(cmd->index, cmd->bindPoint, cmd->pipelineLayout, cmd->bindGroup, std::span(dynamicOffsets, cmd->dynamicOffsetCount));
                ptr = trailing + cmd->dynamicOffsetCount * sizeof(uint32_t);
                break;
            }
            case rhi::CommandType::Bind_Vertex_Buffers:
            {
                const auto* cmd = reinterpret_cast<const rhi::BindVertexBuffersCommand*>(ptr);
                const uint8_t* buffersPtr = alignPointer(ptr + sizeof(rhi::BindVertexBuffersCommand), alignof(Buffer*));
                const Buffer* const* buffers = reinterpret_cast<const Buffer* const*>(buffersPtr);
                const uint8_t* offsetsPtr = alignPointer(buffersPtr + cmd->bindingCount * sizeof(Buffer*), alignof(uint64_t));
                const uint64_t* offsets = reinterpret_cast<const uint64_t*>(offsetsPtr);
                const rhi::Buffer* const* rhiBuffers = reinterpret_cast<const rhi::Buffer* const*>(buffers);
                bindVertexBuffersBackend(cmd->firstBinding, std::span(rhiBuffers, cmd->bindingCount), std::span(offsets, cmd->bindingCount));
                ptr = offsetsPtr + cmd->bindingCount * sizeof(uint64_t);
                break;
            }
            case rhi::CommandType::Bind_Index_Buffer:
            {
                const auto* cmd = reinterpret_cast<const rhi::BindIndexBufferCommand*>(ptr);
                bindIndexBufferBackend(cmd->buffer, cmd->offset, cmd->indexType);
                ptr += sizeof(rhi::BindIndexBufferCommand);
                break;
            }
            case rhi::CommandType::Set_Viewport:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetViewportCommand*>(ptr);
                setViewportBackend(cmd->x, cmd->y, cmd->width, cmd->height, cmd->minDepth, cmd->maxDepth);
                ptr += sizeof(rhi::SetViewportCommand);
                break;
            }
            case rhi::CommandType::Set_Scissor:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetScissorCommand*>(ptr);
                setScissorBackend(cmd->x, cmd->y, cmd->width, cmd->height);
                ptr += sizeof(rhi::SetScissorCommand);
                break;
            }
            case rhi::CommandType::Set_Blend_Constants:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetBlendConstantsCommand*>(ptr);
                setBlendConstantsBackend(cmd->blendConstants);
                ptr += sizeof(rhi::SetBlendConstantsCommand);
                break;
            }
            case rhi::CommandType::Set_Stencil_Reference:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetStencilReferenceCommand*>(ptr);
                setStencilReferenceBackend(cmd->reference);
                ptr += sizeof(rhi::SetStencilReferenceCommand);
                break;
            }
            case rhi::CommandType::Set_Immediate_Data:
            {
                const auto* cmd = reinterpret_cast<const rhi::SetImmediateDataCommand*>(ptr);
                const uint8_t* data = ptr + sizeof(rhi::SetImmediateDataCommand);
                setImmediateDataBackend(cmd->pipelineLayout, cmd->offset, std::span(data, cmd->size));
                ptr = data + cmd->size;
                break;
            }
            case rhi::CommandType::Begin_Render_Pass:
            {
                const auto* cmd = reinterpret_cast<const rhi::BeginRenderPassCommand*>(ptr);
                const uint8_t* attachmentsPtr = alignPointer(ptr + sizeof(rhi::BeginRenderPassCommand), alignof(rhi::ColorAttachmentInfo));
                const auto* colorAttachments = reinterpret_cast<const rhi::ColorAttachmentInfo*>(attachmentsPtr);
                beginRenderPassBackend(cmd->renderArea, cmd->depthStencilAttachment, std::span(colorAttachments, cmd->colorAttachmentCount));
                ptr = attachmentsPtr + cmd->colorAttachmentCount * sizeof(rhi::ColorAttachmentInfo);
                break;
            }
            case rhi::CommandType::End_Render_Pass:
            {
                endRenderPassBackend();
                ptr += sizeof(rhi::EndRenderPassCommand);
                break;
            }
            case rhi::CommandType::Begin_Compute_Pass:
            {
                beginComputePassBackend();
                ptr += sizeof(rhi::BeginComputePassCommand);
                break;
            }
            case rhi::CommandType::End_Compute_Pass:
            {
                endComputePassBackend();
                ptr += sizeof(rhi::EndComputePassCommand);
                break;
            }
            case rhi::CommandType::Draw:
            {
                const auto* cmd = reinterpret_cast<const rhi::DrawCommand*>(ptr);
                drawBackend(cmd->vertexCount, cmd->instanceCount, cmd->firstVertex, cmd->firstInstance);
                ptr += sizeof(rhi::DrawCommand);
                break;
            }
            case rhi::CommandType::Draw_Indexed:
            {
                const auto* cmd = reinterpret_cast<const rhi::DrawIndexedCommand*>(ptr);
                drawIndexedBackend(cmd->indexCount, cmd->instanceCount, cmd->firstIndex, cmd->vertexOffset, cmd->firstInstance);
                ptr += sizeof(rhi::DrawIndexedCommand);
                break;
            }
            case rhi::CommandType::Draw_Indirect:
            {
                const auto* cmd = reinterpret_cast<const rhi::DrawIndirectCommand*>(ptr);
                drawIndirectBackend(cmd->buffer, cmd->offset, cmd->drawCount, cmd->stride);
                ptr += sizeof(rhi::DrawIndirectCommand);
                break;
            }
            case rhi::CommandType::Draw_Indexed_Indirect:
            {
                const auto* cmd = reinterpret_cast<const rhi::DrawIndexedIndirectCommand*>(ptr);
                drawIndexedIndirectBackend(cmd->buffer, cmd->offset, cmd->drawCount, cmd->stride);
                ptr += sizeof(rhi::DrawIndexedIndirectCommand);
                break;
            }
            case rhi::CommandType::Dispatch:
            {
                const auto* cmd = reinterpret_cast<const rhi::DispatchCommand*>(ptr);
                dispatchBackend(cmd->groupCountX, cmd->groupCountY, cmd->groupCountZ);
                ptr += sizeof(rhi::DispatchCommand);
                break;
            }
            case rhi::CommandType::Dispatch_Indirect:
            {
                const auto* cmd = reinterpret_cast<const rhi::DispatchIndirectCommand*>(ptr);
                dispatchIndirectBackend(cmd->buffer, cmd->offset);
                ptr += sizeof(rhi::DispatchIndirectCommand);
                break;
            }
            case rhi::CommandType::Barrier:
            {
                const auto* cmd = reinterpret_cast<const rhi::BarrierCommand*>(ptr);
                const uint8_t* bufferBarriersPtr = alignPointer(ptr + sizeof(rhi::BarrierCommand), alignof(rhi::BufferBarrier));
                const auto* bufferBarriers = reinterpret_cast<const rhi::BufferBarrier*>(bufferBarriersPtr);
                const uint8_t* textureBarriersPtr = alignPointer(bufferBarriersPtr + cmd->bufferBarrierCount * sizeof(rhi::BufferBarrier), alignof(rhi::TextureBarrier));
                const auto* textureBarriers = reinterpret_cast<const rhi::TextureBarrier*>(textureBarriersPtr);
                insertBarriersBackend(
                    std::span(bufferBarriers, cmd->bufferBarrierCount),
                    std::span(textureBarriers, cmd->textureBarrierCount)
                );
                ptr = textureBarriersPtr + cmd->textureBarrierCount * sizeof(rhi::TextureBarrier);
                break;
            }
            case rhi::CommandType::Copy_Buffer:
            {
                const auto* cmd = reinterpret_cast<const rhi::CopyBufferCommand*>(ptr);
                const uint8_t* regionsPtr = alignPointer(ptr + sizeof(rhi::CopyBufferCommand), alignof(rhi::BufferCopyRegion));
                const auto* regions = reinterpret_cast<const rhi::BufferCopyRegion*>(regionsPtr);
                copyBufferBackend(cmd->srcBuffer, cmd->dstBuffer, std::span(regions, cmd->regionCount));
                ptr = regionsPtr + cmd->regionCount * sizeof(rhi::BufferCopyRegion);
                break;
            }
            case rhi::CommandType::Copy_Buffer_To_Texture:
            {
                const auto* cmd = reinterpret_cast<const rhi::CopyBufferToTextureCommand*>(ptr);
                const uint8_t* regionsPtr = alignPointer(ptr + sizeof(rhi::CopyBufferToTextureCommand), alignof(rhi::BufferTextureCopyRegion));
                const auto* regions = reinterpret_cast<const rhi::BufferTextureCopyRegion*>(regionsPtr);
                copyBufferToTextureBackend(cmd->srcBuffer, cmd->dstTexture, std::span(regions, cmd->regionCount));
                ptr = regionsPtr + cmd->regionCount * sizeof(rhi::BufferTextureCopyRegion);
                break;
            }
            case rhi::CommandType::Clear_Buffer:
            {
                const auto* cmd = reinterpret_cast<const rhi::ClearBufferCommand*>(ptr);
                clearBufferBackend(cmd->dstBuffer, cmd->dstOffset, cmd->size);
                ptr += sizeof(rhi::ClearBufferCommand);
                break;
            }
            case rhi::CommandType::Copy_Texture:
            {
                const auto* cmd = reinterpret_cast<const rhi::CopyTextureCommand*>(ptr);
                const uint8_t* regionsPtr = alignPointer(ptr + sizeof(rhi::CopyTextureCommand), alignof(rhi::TextureCopyRegion));
                const auto* regions = reinterpret_cast<const rhi::TextureCopyRegion*>(regionsPtr);
                copyTextureBackend(cmd->srcTexture, cmd->dstTexture, std::span(regions, cmd->regionCount));
                ptr = regionsPtr + cmd->regionCount * sizeof(rhi::TextureCopyRegion);
                break;
            }
            case rhi::CommandType::Copy_Texture_To_Buffer:
            {
                const auto* cmd = reinterpret_cast<const rhi::CopyTextureToBufferCommand*>(ptr);
                const uint8_t* regionsPtr = alignPointer(ptr + sizeof(rhi::CopyTextureToBufferCommand), alignof(rhi::BufferTextureCopyRegion));
                const auto* regions = reinterpret_cast<const rhi::BufferTextureCopyRegion*>(regionsPtr);
                copyTextureToBufferBackend(cmd->srcTexture, cmd->dstBuffer, std::span(regions, cmd->regionCount));
                ptr = regionsPtr + cmd->regionCount * sizeof(rhi::BufferTextureCopyRegion);
                break;
            }
            case rhi::CommandType::Begin_Debug_Group:
            {
                const auto* cmd = reinterpret_cast<const rhi::BeginDebugGroupCommand*>(ptr);
                beginDebugGroupBackend(cmd->label, cmd->color);
                ptr += sizeof(rhi::BeginDebugGroupCommand);
                break;
            }
            case rhi::CommandType::End_Debug_Group:
            {
                endDebugGroupBackend();
                ptr += sizeof(rhi::EndDebugGroupCommand);
                break;
            }
            case rhi::CommandType::Insert_Debug_Marker:
            {
                const auto* cmd = reinterpret_cast<const rhi::InsertDebugMarkerCommand*>(ptr);
                insertDebugMarkerBackend(cmd->label, cmd->color);
                ptr += sizeof(rhi::InsertDebugMarkerCommand);
                break;
            }
            case rhi::CommandType::Native_Callback:
            {
                const auto* cmd = reinterpret_cast<const rhi::NativeCallbackCommand*>(ptr);
                invokeNativeCallbackBackend(*cmd->callback);
                ptr += sizeof(rhi::NativeCallbackCommand);
                break;
            }
            default:
            {
                LOG_ERROR("Unknown RHI command type");
                releaseEncoder();
                return nullptr;
            }
            }
        }

        endCurrentPass();

        WGPUCommandBufferDescriptor commandBufferDesc = {};
        commandBufferDesc.nextInChain = nullptr;
        commandBufferDesc.label = WGPUStringView{ "UnnamedCommandBuffer", WGPU_STRLEN };
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(mEncoder, &commandBufferDesc);
        releaseEncoder();

        return commandBuffer;
#endif
    }

    void CommandList::bindPipelineBackend(const rhi::Pipeline* pipeline)
    {
        auto* wgpuPipeline = static_cast<const Pipeline*>(pipeline);
        if (wgpuPipeline->getType() == rhi::PipelineType::Graphics)
        {
            assert(mRenderPassEncoder != nullptr && "Graphics pipeline must be bound inside render pass");
            wgpuRenderPassEncoderSetPipeline(mRenderPassEncoder, wgpuPipeline->getRenderPipeline());
        }
        else
        {
            assert(mComputePassEncoder != nullptr && "Compute pipeline must be bound inside compute pass");
            wgpuComputePassEncoderSetPipeline(mComputePassEncoder, wgpuPipeline->getComputePipeline());
        }
    }

    void CommandList::setBindGroupBackend(uint32_t index, rhi::PipelineBindPoint bindPoint, const rhi::PipelineLayout* pipelineLayout, const rhi::BindGroup* bindGroup, std::span<const uint32_t> dynamicOffsets)
    {
        auto* wgpuBindGroup = static_cast<const BindGroup*>(bindGroup);
        if (bindPoint == rhi::PipelineBindPoint::Graphics)
        {
            if (mRenderPassEncoder)
            {
                setBindGroupOnEncoder(mRenderPassEncoder, mComputePassEncoder, bindPoint, index, wgpuBindGroup, static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
            }
            mPendingGraphicsBindGroups[index].bindGroup = wgpuBindGroup;
            mPendingGraphicsBindGroups[index].dynamicOffsets.assign(dynamicOffsets.begin(), dynamicOffsets.end());
        }
        else
        {
            if (mComputePassEncoder)
            {
                setBindGroupOnEncoder(mRenderPassEncoder, mComputePassEncoder, bindPoint, index, wgpuBindGroup, static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
            }
            mPendingComputeBindGroups[index].bindGroup = wgpuBindGroup;
            mPendingComputeBindGroups[index].dynamicOffsets.assign(dynamicOffsets.begin(), dynamicOffsets.end());
        }
    }

    void CommandList::bindVertexBuffersBackend(uint32_t firstBinding, std::span<const rhi::Buffer* const> buffers, std::span<const uint64_t> offsets)
    {
        assert(mRenderPassEncoder != nullptr && "BindVertexBuffers must be inside render pass");
        for (size_t i = 0; i < buffers.size(); ++i)
        {
            WGPUBuffer buffer = buffers[i] ? static_cast<const Buffer*>(buffers[i])->getHandle() : nullptr;
            uint64_t offset = offsets[i];
            uint64_t size = WGPU_WHOLE_SIZE;
            wgpuRenderPassEncoderSetVertexBuffer(mRenderPassEncoder, firstBinding + static_cast<uint32_t>(i), buffer, offset, size);
        }
    }

    void CommandList::bindIndexBufferBackend(const rhi::Buffer* buffer, uint64_t offset, rhi::IndexType indexType)
    {
        assert(mRenderPassEncoder != nullptr && "BindIndexBuffer must be inside render pass");
        WGPUBuffer wgpuBuffer = buffer ? static_cast<const Buffer*>(buffer)->getHandle() : nullptr;
        wgpuRenderPassEncoderSetIndexBuffer(mRenderPassEncoder, wgpuBuffer, convertIndexType(indexType), offset, WGPU_WHOLE_SIZE);
    }

    void CommandList::setViewportBackend(float x, float y, float width, float height, float minDepth, float maxDepth)
    {
        assert(mRenderPassEncoder != nullptr && "SetViewport must be inside render pass");
        wgpuRenderPassEncoderSetViewport(mRenderPassEncoder, x, y, width, height, minDepth, maxDepth);
    }

    void CommandList::setScissorBackend(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        assert(mRenderPassEncoder != nullptr && "SetScissor must be inside render pass");
        wgpuRenderPassEncoderSetScissorRect(mRenderPassEncoder, x, y, width, height);
    }

    void CommandList::setBlendConstantsBackend(float4 blendConstants)
    {
        assert(mRenderPassEncoder != nullptr && "SetBlendConstants must be inside render pass");
        WGPUColor color = { blendConstants.x, blendConstants.y, blendConstants.z, blendConstants.w };
        wgpuRenderPassEncoderSetBlendConstant(mRenderPassEncoder, &color);
    }

    void CommandList::setImmediateDataBackend(const rhi::PipelineLayout* pipelineLayout, uint32_t offset, std::span<const uint8_t> data)
    {
        if (mRenderPassEncoder)
        {
            wgpuRenderPassEncoderSetImmediates(mRenderPassEncoder, offset, data.data(), data.size());
        }
        else if (mComputePassEncoder)
        {
            wgpuComputePassEncoderSetImmediates(mComputePassEncoder, offset, data.data(), data.size());
        }
        else
        {
            assert(false && "setImmediateData must be inside a render or compute pass");
        }
    }

    void CommandList::setStencilReferenceBackend(uint32_t reference)
    {
        assert(mRenderPassEncoder != nullptr && "SetStencilReference must be inside render pass");
        wgpuRenderPassEncoderSetStencilReference(mRenderPassEncoder, reference);
    }

    void CommandList::beginRenderPassBackend(rhi::RenderArea renderArea, const rhi::DepthStencilAttachmentInfo& depthStencilAttachment, std::span<const rhi::ColorAttachmentInfo> colorAttachments)
    {
        assert(mRenderPassEncoder == nullptr && mComputePassEncoder == nullptr && "Cannot nest render pass");

        std::vector<WGPURenderPassColorAttachment> wgpuColorAttachments(colorAttachments.size());
        for (size_t i = 0; i < colorAttachments.size(); ++i)
        {
            const auto& src = colorAttachments[i];
            wgpuColorAttachments[i] = {};
            wgpuColorAttachments[i].view = src.textureView ? static_cast<const TextureView*>(src.textureView)->getHandle() : nullptr;
            wgpuColorAttachments[i].depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
            wgpuColorAttachments[i].loadOp = convertLoadOp(src.loadOp);
            wgpuColorAttachments[i].storeOp = convertStoreOp(src.storeOp);
            wgpuColorAttachments[i].clearValue = convertClearColor(src.clearColor);
        }

        WGPURenderPassDepthStencilAttachment wgpuDepthStencilAttachment = {};
        const WGPURenderPassDepthStencilAttachment* pDepthStencilAttachment = nullptr;
        if (depthStencilAttachment.textureView)
        {
            const auto& ds = depthStencilAttachment;
            wgpuDepthStencilAttachment.view = static_cast<const TextureView*>(ds.textureView)->getHandle();

            wgpuDepthStencilAttachment.depthReadOnly =
                ds.state == rhi::ResourceState::Depth_Attachment_Read ||
                ds.state == rhi::ResourceState::Depth_Stencil_Attachment_Read ||
                ds.state == rhi::ResourceState::Depth_Read_Stencil_Attachment;
            bool depthDisabled = rhi::isStencilFormat(ds.textureView->getDesc().format) ||
                wgpuDepthStencilAttachment.depthReadOnly;
            if (!depthDisabled)
            {
                wgpuDepthStencilAttachment.depthLoadOp = convertLoadOp(ds.depthLoadOp);
                wgpuDepthStencilAttachment.depthStoreOp = convertStoreOp(ds.depthStoreOp);
            }
            wgpuDepthStencilAttachment.depthClearValue = ds.clearDepthStencil.depth;

            wgpuDepthStencilAttachment.stencilReadOnly =
                ds.state == rhi::ResourceState::Stencil_Attachment_Read ||
                ds.state == rhi::ResourceState::Depth_Stencil_Read_Attachment ||
                ds.state == rhi::ResourceState::Depth_Stencil_Attachment_Read;
            bool stencilDisabled = rhi::isDepthFormat(ds.textureView->getDesc().format) ||
                wgpuDepthStencilAttachment.stencilReadOnly;
            if (!stencilDisabled)
            {
                wgpuDepthStencilAttachment.stencilLoadOp = convertLoadOp(ds.stencilLoadOp);
                wgpuDepthStencilAttachment.stencilStoreOp = convertStoreOp(ds.stencilStoreOp);
            }
            wgpuDepthStencilAttachment.stencilClearValue = ds.clearDepthStencil.stencil;

            pDepthStencilAttachment = &wgpuDepthStencilAttachment;
        }

        WGPURenderPassDescriptor renderPassDesc = {};
        renderPassDesc.nextInChain = nullptr;
        renderPassDesc.label = WGPUStringView{ "UnnamedRenderPass", WGPU_STRLEN };
        renderPassDesc.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
        renderPassDesc.colorAttachments = wgpuColorAttachments.data();
        renderPassDesc.depthStencilAttachment = pDepthStencilAttachment;
        renderPassDesc.occlusionQuerySet = nullptr;
        renderPassDesc.timestampWrites = nullptr;

        mRenderPassEncoder = wgpuCommandEncoderBeginRenderPass(mEncoder, &renderPassDesc);

        for (uint32_t i = 0; i < 4; ++i)
        {
            const auto& pending = mPendingGraphicsBindGroups[i];
            if (pending.bindGroup)
            {
                setBindGroupOnEncoder(mRenderPassEncoder, mComputePassEncoder, rhi::PipelineBindPoint::Graphics, i, pending.bindGroup, static_cast<uint32_t>(pending.dynamicOffsets.size()), pending.dynamicOffsets.data());
            }
        }
    }

    void CommandList::endRenderPassBackend()
    {
        assert(mRenderPassEncoder != nullptr && "EndRenderPass without BeginRenderPass");
        wgpuRenderPassEncoderEnd(mRenderPassEncoder);
        wgpuRenderPassEncoderRelease(mRenderPassEncoder);
        mRenderPassEncoder = nullptr;
    }

    void CommandList::beginComputePassBackend()
    {
        assert(mRenderPassEncoder == nullptr && mComputePassEncoder == nullptr && "Cannot nest compute pass");
        WGPUComputePassDescriptor computePassDesc = {};
        computePassDesc.nextInChain = nullptr;
        computePassDesc.label = WGPUStringView{ "UnnamedComputePass", WGPU_STRLEN };
        computePassDesc.timestampWrites = nullptr;
        mComputePassEncoder = wgpuCommandEncoderBeginComputePass(mEncoder, &computePassDesc);

        for (uint32_t i = 0; i < 4; ++i)
        {
            const auto& pending = mPendingComputeBindGroups[i];
            if (pending.bindGroup)
            {
                setBindGroupOnEncoder(mRenderPassEncoder, mComputePassEncoder, rhi::PipelineBindPoint::Compute, i, pending.bindGroup, static_cast<uint32_t>(pending.dynamicOffsets.size()), pending.dynamicOffsets.data());
            }
        }
    }

    void CommandList::endComputePassBackend()
    {
        assert(mComputePassEncoder != nullptr && "EndComputePass without BeginComputePass");
        wgpuComputePassEncoderEnd(mComputePassEncoder);
        wgpuComputePassEncoderRelease(mComputePassEncoder);
        mComputePassEncoder = nullptr;
    }

    void CommandList::drawBackend(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
    {
        assert(mRenderPassEncoder != nullptr && "Draw must be inside render pass");
        wgpuRenderPassEncoderDraw(mRenderPassEncoder, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    void CommandList::drawIndexedBackend(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
    {
        assert(mRenderPassEncoder != nullptr && "DrawIndexed must be inside render pass");
        wgpuRenderPassEncoderDrawIndexed(mRenderPassEncoder, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
    }

    void CommandList::drawIndirectBackend(const rhi::Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
    {
        assert(mRenderPassEncoder != nullptr && "DrawIndirect must be inside render pass");
        WGPUBuffer wgpuBuffer = buffer ? static_cast<const Buffer*>(buffer)->getHandle() : nullptr;
        for (uint32_t i = 0; i < drawCount; ++i)
        {
            uint64_t o = offset + i * stride;
            wgpuRenderPassEncoderDrawIndirect(mRenderPassEncoder, wgpuBuffer, o);
        }
    }

    void CommandList::drawIndexedIndirectBackend(const rhi::Buffer* buffer, uint64_t offset, uint32_t drawCount, uint32_t stride)
    {
        assert(mRenderPassEncoder != nullptr && "DrawIndexedIndirect must be inside render pass");
        WGPUBuffer wgpuBuffer = buffer ? static_cast<const Buffer*>(buffer)->getHandle() : nullptr;
        for (uint32_t i = 0; i < drawCount; ++i)
        {
            uint64_t o = offset + i * stride;
            wgpuRenderPassEncoderDrawIndexedIndirect(mRenderPassEncoder, wgpuBuffer, o);
        }
    }

    void CommandList::dispatchBackend(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
    {
        assert(mComputePassEncoder != nullptr && "Dispatch must be inside compute pass");
        wgpuComputePassEncoderDispatchWorkgroups(mComputePassEncoder, groupCountX, groupCountY, groupCountZ);
    }

    void CommandList::dispatchIndirectBackend(const rhi::Buffer* buffer, uint64_t offset)
    {
        assert(mComputePassEncoder != nullptr && "DispatchIndirect must be inside compute pass");
        WGPUBuffer wgpuBuffer = buffer ? static_cast<const Buffer*>(buffer)->getHandle() : nullptr;
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(mComputePassEncoder, wgpuBuffer, offset);
    }

    void CommandList::insertBarriersBackend(std::span<const rhi::BufferBarrier> bufferBarriers, std::span<const rhi::TextureBarrier> textureBarriers)
    {
        assert(mRenderPassEncoder == nullptr && mComputePassEncoder == nullptr && "Barrier cannot be inside pass");
        (void)bufferBarriers;
        (void)textureBarriers;
    }

    void CommandList::copyBufferBackend(const rhi::Buffer* srcBuffer, const rhi::Buffer* dstBuffer, std::span<const rhi::BufferCopyRegion> regions)
    {
        assert(mRenderPassEncoder == nullptr && mComputePassEncoder == nullptr && "CopyBuffer cannot be inside pass");
        WGPUBuffer wgpuSrcBuffer = srcBuffer ? static_cast<const Buffer*>(srcBuffer)->getHandle() : nullptr;
        WGPUBuffer wgpuDstBuffer = dstBuffer ? static_cast<const Buffer*>(dstBuffer)->getHandle() : nullptr;

        for (const auto& region : regions)
        {
            wgpuCommandEncoderCopyBufferToBuffer(
                mEncoder,
                wgpuSrcBuffer, region.srcOffset,
                wgpuDstBuffer, region.dstOffset,
                region.size);
        }
    }

    void CommandList::copyBufferToTextureBackend(const rhi::Buffer* srcBuffer, const rhi::Texture* dstTexture, std::span<const rhi::BufferTextureCopyRegion> regions)
    {
        assert(mRenderPassEncoder == nullptr && mComputePassEncoder == nullptr && "CopyBufferToTexture cannot be inside pass");
        WGPUBuffer wgpuSrcBuffer = srcBuffer ? static_cast<const Buffer*>(srcBuffer)->getHandle() : nullptr;
        WGPUTexture wgpuDstTexture = dstTexture ? static_cast<const Texture*>(dstTexture)->getHandle() : nullptr;
        const rhi::TextureFormat format = dstTexture ? static_cast<const Texture*>(dstTexture)->getDesc().format : rhi::TextureFormat::Undefined;
        rhi::TextureFormatBlockInfo blockInfo = rhi::getTextureFormatBlockInfo(format);

        const bool dstIs3D = dstTexture && static_cast<const Texture*>(dstTexture)->getDesc().type == rhi::TextureType::Texture_3D;

        for (const auto& r : regions)
        {
            WGPUExtent3D extent = { r.textureExtent.x, r.textureExtent.y, r.textureExtent.z };
            if (blockInfo.blockWidth > 1 || blockInfo.blockHeight > 1)
            {
                extent.width = ((extent.width + blockInfo.blockWidth - 1) / blockInfo.blockWidth) * blockInfo.blockWidth;
                extent.height = ((extent.height + blockInfo.blockHeight - 1) / blockInfo.blockHeight) * blockInfo.blockHeight;
            }

            WGPUTexelCopyBufferInfo sourceInfo;
            sourceInfo.layout.bytesPerRow = r.bytesPerRow;
            sourceInfo.layout.rowsPerImage = r.rowsPerTexture;
            sourceInfo.buffer = wgpuSrcBuffer;

            WGPUTexelCopyTextureInfo destinationInfo;
            destinationInfo.texture = wgpuDstTexture;
            destinationInfo.mipLevel = r.slice.mipLevel;
            destinationInfo.origin = { static_cast<uint32_t>(r.textureOffset.x), static_cast<uint32_t>(r.textureOffset.y), static_cast<uint32_t>(r.textureOffset.z) };
            destinationInfo.aspect = WGPUTextureAspect_All;

            if (dstIs3D)
            {
                sourceInfo.layout.offset = r.bufferOffset;
                wgpuCommandEncoderCopyBufferToTexture(mEncoder, &sourceInfo, &destinationInfo, &extent);
            }
            else
            {
                uint64_t layerBytes = static_cast<uint64_t>(r.bytesPerRow) * r.rowsPerTexture;
                for (uint32_t layer = 0; layer < r.slice.layerCount; ++layer)
                {
                    sourceInfo.layout.offset = r.bufferOffset + layer * layerBytes;
                    destinationInfo.origin.z = r.slice.baseLayer + layer;
                    wgpuCommandEncoderCopyBufferToTexture(mEncoder, &sourceInfo, &destinationInfo, &extent);
                }
            }
        }
    }

    void CommandList::clearBufferBackend(const rhi::Buffer* dstBuffer, uint64_t dstOffset, uint64_t size)
    {
        assert(mRenderPassEncoder == nullptr && mComputePassEncoder == nullptr && "ClearBuffer cannot be inside pass");
        WGPUBuffer wgpuBuffer = dstBuffer ? static_cast<const Buffer*>(dstBuffer)->getHandle() : nullptr;
        wgpuCommandEncoderClearBuffer(mEncoder, wgpuBuffer, dstOffset, size);
    }

    void CommandList::copyTextureBackend(const rhi::Texture* srcTexture, const rhi::Texture* dstTexture, std::span<const rhi::TextureCopyRegion> regions)
    {
        assert(mRenderPassEncoder == nullptr && mComputePassEncoder == nullptr && "CopyTexture cannot be inside pass");
        WGPUTexture wgpuSrcTexture = srcTexture ? static_cast<const Texture*>(srcTexture)->getHandle() : nullptr;
        WGPUTexture wgpuDstTexture = dstTexture ? static_cast<const Texture*>(dstTexture)->getHandle() : nullptr;

        const bool srcIs3D = srcTexture && static_cast<const Texture*>(srcTexture)->getDesc().type == rhi::TextureType::Texture_3D;
        const bool dstIs3D = dstTexture && static_cast<const Texture*>(dstTexture)->getDesc().type == rhi::TextureType::Texture_3D;

        for (const auto& r : regions)
        {
            WGPUExtent3D extent = { r.extent.x, r.extent.y, r.extent.z };

            WGPUTexelCopyTextureInfo sourceInfo;
            sourceInfo.texture = wgpuSrcTexture;
            sourceInfo.mipLevel = r.srcSlice.mipLevel;
            sourceInfo.origin = { static_cast<uint32_t>(r.srcOffset.x), static_cast<uint32_t>(r.srcOffset.y), static_cast<uint32_t>(r.srcOffset.z) };
            sourceInfo.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyTextureInfo destinationInfo;
            destinationInfo.texture = wgpuDstTexture;
            destinationInfo.mipLevel = r.dstSlice.mipLevel;
            destinationInfo.origin = { static_cast<uint32_t>(r.dstOffset.x), static_cast<uint32_t>(r.dstOffset.y), static_cast<uint32_t>(r.dstOffset.z) };
            destinationInfo.aspect = WGPUTextureAspect_All;

            if (srcIs3D || dstIs3D)
            {
                wgpuCommandEncoderCopyTextureToTexture(mEncoder, &sourceInfo, &destinationInfo, &extent);
            }
            else
            {
                uint32_t layerCount = r.srcSlice.layerCount;
                for (uint32_t layer = 0; layer < layerCount; ++layer)
                {
                    sourceInfo.origin.z = r.srcSlice.baseLayer + layer;
                    destinationInfo.origin.z = r.dstSlice.baseLayer + layer;
                    wgpuCommandEncoderCopyTextureToTexture(mEncoder, &sourceInfo, &destinationInfo, &extent);
                }
            }
        }
    }

    void CommandList::copyTextureToBufferBackend(const rhi::Texture* srcTexture, const rhi::Buffer* dstBuffer, std::span<const rhi::BufferTextureCopyRegion> regions)
    {
        assert(mRenderPassEncoder == nullptr && mComputePassEncoder == nullptr && "CopyTextureToBuffer cannot be inside pass");
        WGPUTexture wgpuSrcTexture = srcTexture ? static_cast<const Texture*>(srcTexture)->getHandle() : nullptr;
        const rhi::TextureFormat format = srcTexture ? static_cast<const Texture*>(srcTexture)->getDesc().format : rhi::TextureFormat::Undefined;
        rhi::TextureFormatBlockInfo blockInfo = rhi::getTextureFormatBlockInfo(format);
        WGPUBuffer wgpuDstBuffer = dstBuffer ? static_cast<const Buffer*>(dstBuffer)->getHandle() : nullptr;

        const bool srcIs3D = srcTexture && static_cast<const Texture*>(srcTexture)->getDesc().type == rhi::TextureType::Texture_3D;

        for (const auto& r : regions)
        {
            WGPUExtent3D extent = { r.textureExtent.x, r.textureExtent.y, r.textureExtent.z };
            if (blockInfo.blockWidth > 1 || blockInfo.blockHeight > 1)
            {
                extent.width = ((extent.width + blockInfo.blockWidth - 1) / blockInfo.blockWidth) * blockInfo.blockWidth;
                extent.height = ((extent.height + blockInfo.blockHeight - 1) / blockInfo.blockHeight) * blockInfo.blockHeight;
            }

            WGPUTexelCopyTextureInfo sourceInfo;
            sourceInfo.texture = wgpuSrcTexture;
            sourceInfo.mipLevel = r.slice.mipLevel;
            sourceInfo.origin = { static_cast<uint32_t>(r.textureOffset.x), static_cast<uint32_t>(r.textureOffset.y), static_cast<uint32_t>(r.textureOffset.z) };
            sourceInfo.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyBufferInfo destinationInfo;
            destinationInfo.layout.bytesPerRow = r.bytesPerRow;
            destinationInfo.layout.rowsPerImage = r.rowsPerTexture;
            destinationInfo.buffer = wgpuDstBuffer;

            if (srcIs3D)
            {
                destinationInfo.layout.offset = r.bufferOffset;
                wgpuCommandEncoderCopyTextureToBuffer(mEncoder, &sourceInfo, &destinationInfo, &extent);
            }
            else
            {
                uint64_t layerBytes = static_cast<uint64_t>(r.bytesPerRow) * r.rowsPerTexture;
                for (uint32_t layer = 0; layer < r.slice.layerCount; ++layer)
                {
                    sourceInfo.origin.z = r.slice.baseLayer + layer;
                    destinationInfo.layout.offset = r.bufferOffset + layer * layerBytes;
                    wgpuCommandEncoderCopyTextureToBuffer(mEncoder, &sourceInfo, &destinationInfo, &extent);
                }
            }
        }
    }

    void CommandList::beginDebugGroupBackend(HString label, float4 color)
    {
        WGPUStringView wgpuLabel = WGPUStringView{ label.toCString(), WGPU_STRLEN };
        wgpuCommandEncoderPushDebugGroup(mEncoder, wgpuLabel);
    }

    void CommandList::endDebugGroupBackend()
    {
        wgpuCommandEncoderPopDebugGroup(mEncoder);
    }

    void CommandList::insertDebugMarkerBackend(HString label, float4 color)
    {
        WGPUStringView wgpuLabel = WGPUStringView{ label.toCString(), WGPU_STRLEN };
        wgpuCommandEncoderInsertDebugMarker(mEncoder, wgpuLabel);
    }

    void CommandList::invokeNativeCallbackBackend(const rhi::NativeRenderCallback& callback)
    {
        // 只支持在 render pass 内调用 —— 这条指令存在的目的就是给 ImGui_ImplWGPU_RenderDrawData
        // 这类需要 WGPURenderPassEncoder(而不是 WGPUCommandEncoder/WGPUCommandBuffer)的第三方
        // 调用提供句柄, 见 rhi::NativeRenderCallback 的注释。decode() 重放到这一条时,
        // mRenderPassEncoder 只有在 Begin_Render_Pass 和 End_Render_Pass 之间才是非空的。
        assert(mRenderPassEncoder != nullptr && "invokeNativeCallback (WebGPU) must be called inside a render pass");
        rhi::NativePassHandle handle;
        handle.type = rhi::NativePassHandleType::WebGPU_Render_Pass_Encoder;
        handle.handle = mRenderPassEncoder;
        callback(handle);
    }
}
