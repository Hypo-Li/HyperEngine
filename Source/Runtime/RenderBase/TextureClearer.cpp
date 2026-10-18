#include "TextureClearer.h"
#include "PassShaderUtils.h"
#include "RenderBackend.h"

#include <ClearStorageTexture_R11G11B10F.generated.h>
#include <ClearStorageTexture_R16.generated.h>
#include <ClearStorageTexture_R16F.generated.h>
#include <ClearStorageTexture_R32F.generated.h>
#include <ClearStorageTexture_R32UI.generated.h>
#include <ClearStorageTexture_R8.generated.h>
#include <ClearStorageTexture_R8UI.generated.h>
#include <ClearStorageTexture_RG32F.generated.h>
#include <ClearStorageTexture_RG8.generated.h>
#include <ClearStorageTexture_RGB10A2.generated.h>
#include <ClearStorageTexture_RGBA16F.generated.h>
#include <ClearStorageTexture_RGBA8.generated.h>

#include <algorithm>
#include <cassert>
#include <cstdlib>

namespace hyp
{
    namespace
    {
        struct StorageClearShader
        {
            const char* name;
            std::span<const uint32_t> spirv;
            std::string_view wgsl;
            std::span<const rhi::PassShaderBindingDesc> bindings;
        };

#define STORAGE_CLEAR_SHADER(format, shaderName) \
        case rhi::TextureFormat::format: \
            return { #shaderName, GET_PASS_SHADER_BYTECODE(shaderName, Compute), generated::k##shaderName##_Bindings }

        StorageClearShader selectStorageClearShader(rhi::TextureFormat format)
        {
            switch (format)
            {
            STORAGE_CLEAR_SHADER(RGBA16_SFLOAT, ClearStorageTexture_RGBA16F);
            STORAGE_CLEAR_SHADER(B10G11R11_UFLOAT_PACK32, ClearStorageTexture_R11G11B10F);
            STORAGE_CLEAR_SHADER(RGBA8_UNORM, ClearStorageTexture_RGBA8);
            STORAGE_CLEAR_SHADER(A2BGR10_UNORM_PACK32, ClearStorageTexture_RGB10A2);
            STORAGE_CLEAR_SHADER(R32_SFLOAT, ClearStorageTexture_R32F);
            STORAGE_CLEAR_SHADER(RG32_SFLOAT, ClearStorageTexture_RG32F);
            STORAGE_CLEAR_SHADER(R32_UINT, ClearStorageTexture_R32UI);
            STORAGE_CLEAR_SHADER(R8_UINT, ClearStorageTexture_R8UI);
            STORAGE_CLEAR_SHADER(R16_UNORM, ClearStorageTexture_R16);
            STORAGE_CLEAR_SHADER(R16_SFLOAT, ClearStorageTexture_R16F);
            STORAGE_CLEAR_SHADER(R8_UNORM, ClearStorageTexture_R8);
            STORAGE_CLEAR_SHADER(RG8_UNORM, ClearStorageTexture_RG8);
            default:
                break;
            }
            assert(false && "TextureClearer: no ClearStorageTexture shader for this storage texture format");
            std::abort();
        }

#undef STORAGE_CLEAR_SHADER

        uint2 getViewExtent(const rhi::TextureView& view)
        {
            const rhi::TextureDesc& desc = view.getTexture().getDesc();
            const uint32_t baseMip = view.getDesc().range.baseMip;
            return uint2(std::max(1u, desc.extent.x >> baseMip), std::max(1u, desc.extent.y >> baseMip));
        }
    }

    TextureClearer::TextureClearer(RenderBackend& renderBackend) :
        mRenderBackend(renderBackend)
    {
    }

    TextureClearer::~TextureClearer() = default;

    TextureClearer::StorageClearPipeline& TextureClearer::getStorageClearPipeline(rhi::TextureFormat format)
    {
        auto it = mStorageClearPipelines.find(format);
        if (it != mStorageClearPipelines.end())
            return it->second;

        const StorageClearShader source = selectStorageClearShader(format);

        StorageClearPipeline entry;
        entry.shader = std::make_unique<PassShader>(
            mRenderBackend.getDevice(),
            source.name,
            makeComputeBytecode(mRenderBackend.getRHIBackend(), source.spirv, source.wgsl)
        );
        entry.bindGroupLayout = createPassBindGroupLayout(mRenderBackend, source.bindings);

        auto& pipelineCache = mRenderBackend.getPipelineCache();
        PipelineCache::PipelineLayoutKey layoutKey{};
        layoutKey.bindGroupLayouts[0] = entry.bindGroupLayout.get();
        TRef<rhi::PipelineLayout> pipelineLayout = pipelineCache.getOrCreateLayout(layoutKey);

        PipelineCache::ComputeKey pipelineKey;
        pipelineKey.shader = entry.shader->getVariant();
        pipelineKey.pipelineLayout = pipelineLayout.get();
        entry.pipeline = pipelineCache.getOrCreateComputePipeline(pipelineKey);

        return mStorageClearPipelines.emplace(format, std::move(entry)).first->second;
    }

    void TextureClearer::clearStorageTexture(rhi::CommandList& cmdList, const rhi::TextureView& view, rhi::ResourceState oldState, rhi::ResourceState newState)
    {
        StorageClearPipeline& clearPipeline = getStorageClearPipeline(view.getDesc().format);

        TRef<rhi::BindGroup> bindGroup = mRenderBackend.getDevice().createBindGroup(*clearPipeline.bindGroupLayout);
        const rhi::TextureView* textureViews[] = { &view };
        rhi::BindGroupEntry bindEntry;
        bindEntry.binding = 0;
        bindEntry.type = rhi::BindingType::Storage_Texture;
        bindEntry.index = 0;
        bindEntry.textures = textureViews;
        bindGroup->update({ &bindEntry, 1 });

        rhi::TextureBarrier barrier;
        barrier.texture = &view.getTexture();
        barrier.range = view.getDesc().range;
        barrier.oldState = oldState;
        barrier.newState = rhi::ResourceState::Compute_Shader_Storage_Write;
        cmdList.insertBarriers({}, { &barrier, 1 });

        const uint2 extent = getViewExtent(view);
        const uint32_t kThreadsPerGroup = 8;
        cmdList.beginComputePass();
        cmdList.bindPipeline(clearPipeline.pipeline.get());
        cmdList.setBindGroup(0, rhi::PipelineBindPoint::Compute, &clearPipeline.pipeline->getLayout(), bindGroup.get(), {});
        cmdList.dispatch((extent.x + kThreadsPerGroup - 1) / kThreadsPerGroup, (extent.y + kThreadsPerGroup - 1) / kThreadsPerGroup, 1);
        cmdList.endComputePass();

        barrier.oldState = rhi::ResourceState::Compute_Shader_Storage_Write;
        barrier.newState = newState;
        cmdList.insertBarriers({}, { &barrier, 1 });

        mRenderBackend.getDeferredTaskQueue().push(
            [bindGroup = std::move(bindGroup)]() mutable { bindGroup = nullptr; },
            kMaxFramesInFlight
        );
    }

    void TextureClearer::clearAttachmentTexture(rhi::CommandList& cmdList, const rhi::TextureView& view, rhi::ResourceState oldState, rhi::ResourceState newState)
    {
        const rhi::TextureAspect aspect = view.getDesc().range.aspect;
        const bool isColor = aspect == rhi::TextureAspect::Color;
        const bool hasStencil = enumHasAnyFlags(aspect, rhi::TextureAspect::Stencil);

        rhi::ResourceState attachmentState = rhi::ResourceState::Color_Attachment_Write;
        if (!isColor)
            attachmentState = hasStencil ? rhi::ResourceState::Depth_Stencil_Attachment : rhi::ResourceState::Depth_Attachment;

        rhi::TextureBarrier barrier;
        barrier.texture = &view.getTexture();
        barrier.range = view.getDesc().range;
        barrier.oldState = oldState;
        barrier.newState = attachmentState;
        cmdList.insertBarriers({}, { &barrier, 1 });

        const uint2 extent = getViewExtent(view);
        const rhi::RenderArea renderArea{ 0, 0, extent.x, extent.y };
        if (isColor)
        {
            rhi::ColorAttachmentInfo colorAttachment{};
            colorAttachment.textureView = &view;
            colorAttachment.loadOp = rhi::AttachmentLoadOp::Clear;
            colorAttachment.storeOp = rhi::AttachmentStoreOp::Store;
            colorAttachment.clearColor = rhi::ClearColorValue{ float4(0.0f) };
            cmdList.beginRenderPass(renderArea, rhi::DepthStencilAttachmentInfo{}, { &colorAttachment, 1 });
        }
        else
        {
            rhi::DepthStencilAttachmentInfo depthStencilAttachment{};
            depthStencilAttachment.textureView = &view;
            depthStencilAttachment.state = attachmentState;
            depthStencilAttachment.depthLoadOp = rhi::AttachmentLoadOp::Clear;
            depthStencilAttachment.depthStoreOp = rhi::AttachmentStoreOp::Store;
            depthStencilAttachment.stencilLoadOp = hasStencil ? rhi::AttachmentLoadOp::Clear : rhi::AttachmentLoadOp::Dont_Care;
            depthStencilAttachment.stencilStoreOp = hasStencil ? rhi::AttachmentStoreOp::Store : rhi::AttachmentStoreOp::None;
            depthStencilAttachment.clearDepthStencil = { 0.0f, 0u };
            cmdList.beginRenderPass(renderArea, depthStencilAttachment, {});
        }
        cmdList.endRenderPass();

        barrier.oldState = attachmentState;
        barrier.newState = newState;
        cmdList.insertBarriers({}, { &barrier, 1 });
    }
}
