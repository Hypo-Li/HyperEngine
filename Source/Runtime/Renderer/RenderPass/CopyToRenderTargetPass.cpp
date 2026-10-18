#include "CopyToRenderTargetPass.h"
#include "RenderPassUtils.h"

#include <CopyTexture.generated.h>

#include <cassert>
#include <unordered_map>

namespace hyp
{
    struct ResolveSceneColorData
    {
        uint32_t viewId;
        rg::TextureHandle sceneColor;
        rg::TextureHandle resolvedSceneColor;
        uint2 extent;
    };

    struct ResolveSceneColorStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<1>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    struct CopyToRTData
    {
        uint32_t viewId;
        rg::TextureHandle src;
        rg::TextureHandle dst;
        float4 viewport;
    };

    struct CopyToRTStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<1>> perViewCaches;
        std::unordered_map<uint32_t, TRef<rhi::Pipeline>> pipelines;
    };

    void CopyToRenderTargetPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 extent(
            static_cast<uint32_t>(viewport.z),
            static_cast<uint32_t>(viewport.w));
        const HString resolvedSceneColorName = "ResolvedSceneColor"_hs(viewId);

        renderGraph.addGraphicsPass<ResolveSceneColorData>(
            "ResolveSceneColor"_hs,
            [&](rg::RenderGraph::Builder& builder, ResolveSceneColorData& data)
            {
                data.viewId = viewId;
                data.extent = extent;

                auto& blackboard = builder.getBlackboard();
                auto sceneColor =
                    blackboard.get<rg::TextureHandle>("SceneColor"_hs(viewId));
                data.sceneColor = builder.sample(sceneColor);

                rhi::TextureDesc resolvedDesc;
                resolvedDesc.type = rhi::TextureType::Texture_2D;
                resolvedDesc.format = rhi::TextureFormat::RGBA8_UNORM;
                resolvedDesc.extent = {extent.x, extent.y, 1};
                resolvedDesc.mipCount = 1;
                resolvedDesc.layerCount = 1;
                resolvedDesc.usage =
                    rhi::TextureUsage::Color_Attachment |
                    rhi::TextureUsage::Sampled;
                auto resolvedSceneColor =
                    builder.createTexture(resolvedSceneColorName, resolvedDesc);
                data.resolvedSceneColor = builder.addColorAttachment(
                    resolvedSceneColor,
                    rhi::AttachmentLoadOp::Dont_Care);
                blackboard.put(resolvedSceneColorName, data.resolvedSceneColor);
            },
            [&rendererContext](rg::ExecuteContext& context, ResolveSceneColorData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<
                    CopyToRenderTargetPass,
                    ResolveSceneColorStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "CopyTexture",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(CopyTexture, Vertex),
                            GET_PASS_SHADER_BYTECODE(CopyTexture, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(
                        renderBackend,
                        generated::kCopyTexture_Bindings);
                }

                const rhi::TextureView* sceneColor =
                    context.getTexture(data.sceneColor);
                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];
                if (bindGroupCache.needUpdate(sceneColor))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kCopyTexture_Bindings);
                            writer.setTexture("SourceTexture", sceneColor);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createResolvePipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout);
                }

                auto& cmdList = context.getCommandList();
                setViewportAndScissor(
                    cmdList,
                    float4(
                        0.0f,
                        0.0f,
                        static_cast<float>(data.extent.x),
                        static_cast<float>(data.extent.y)
                    )
                );
                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    0,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.draw(3, 1, 0, 0);
            }
        );

        renderGraph.addGraphicsPass<CopyToRTData>(
            "CopyToRenderTarget"_hs,
            [&](rg::RenderGraph::Builder& builder, CopyToRTData& data)
            {
                builder.markSideEffect();

                data.viewId = viewId;
                data.viewport = viewport;

                auto& blackboard = builder.getBlackboard();
                auto src =
                    blackboard.get<rg::TextureHandle>(resolvedSceneColorName);
                data.src = builder.sample(src);

                auto dst =
                    blackboard.get<rg::TextureHandle>(view.getFinalRTName());
                data.dst = builder.addColorAttachment(
                    dst,
                    rhi::AttachmentLoadOp::Clear);
            },
            [&rendererContext](rg::ExecuteContext& context, CopyToRTData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<
                    CopyToRenderTargetPass,
                    CopyToRTStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "CopyTexture",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(CopyTexture, Vertex),
                            GET_PASS_SHADER_BYTECODE(CopyTexture, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(
                        renderBackend,
                        generated::kCopyTexture_Bindings);
                }

                const rhi::TextureView* src = context.getTexture(data.src);
                const rhi::TextureView* dst = context.getTexture(data.dst);
                const rhi::TextureFormat dstFormat =
                    dst->getTexture().getDesc().format;

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];
                if (bindGroupCache.needUpdate(src))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(
                                generated::kCopyTexture_Bindings);
                            writer.setTexture("SourceTexture", src);
                            writer.apply(bindGroup);
                        }
                    );
                }

                TRef<rhi::Pipeline>& pipeline =
                    storage.pipelines[static_cast<uint32_t>(dstFormat)];
                if (!pipeline)
                {
                    pipeline = createCopyPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout,
                        dstFormat);
                }

                auto& cmdList = context.getCommandList();
                setViewportAndScissor(cmdList, data.viewport);
                cmdList.bindPipeline(pipeline.get());
                cmdList.setBindGroup(
                    0,
                    rhi::PipelineBindPoint::Graphics,
                    &pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.draw(3, 1, 0, 0);
            }
        );
    }

    rhi::Pipeline* CopyToRenderTargetPass::createResolvePipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout
    )
    {
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        PipelineCache::PipelineLayoutKey layoutKey{};
        layoutKey.bindGroupLayouts[0] = &bindGroupLayout;
        TRef<rhi::PipelineLayout> pipelineLayout = pipelineCache.getOrCreateLayout(layoutKey);

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.attachmentFormats =
            AttachmentFormats::get<
                rhi::TextureFormat::Undefined,
                rhi::TextureFormat::RGBA16_SFLOAT>();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

    rhi::Pipeline* CopyToRenderTargetPass::createCopyPipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout,
        rhi::TextureFormat dstFormat
    )
    {
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();
        PipelineCache::PipelineLayoutKey layoutKey{};
        layoutKey.bindGroupLayouts[0] = &bindGroupLayout;
        TRef<rhi::PipelineLayout> pipelineLayout =
            pipelineCache.getOrCreateLayout(layoutKey);

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
        pipelineKey.pipelineState.shader = shaderVariant;

        AttachmentFormats* attachmentFormats = nullptr;
        switch (dstFormat)
        {
        case rhi::TextureFormat::BGRA8_UNORM: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::BGRA8_UNORM>(); break;
        case rhi::TextureFormat::RGBA8_UNORM: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA8_UNORM>(); break;
        case rhi::TextureFormat::BGRA8_SRGB: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::BGRA8_SRGB>(); break;
        case rhi::TextureFormat::RGBA8_SRGB: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA8_SRGB>(); break;
        case rhi::TextureFormat::A2BGR10_UNORM_PACK32: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::A2BGR10_UNORM_PACK32>(); break;
        case rhi::TextureFormat::RGBA16_SFLOAT: attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::Undefined, rhi::TextureFormat::RGBA16_SFLOAT>(); break;
        default: assert(false && "Unsupported destination format for CopyToRenderTargetPass");
        }

        pipelineKey.pipelineState.attachmentFormats = attachmentFormats;
        pipelineKey.pipelineLayout = pipelineLayout.get();
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
