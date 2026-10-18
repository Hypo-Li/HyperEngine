#include "ScreenProbeDiffuseIndirectCompositePass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <ScreenProbeDiffuseIndirectComposite.generated.h>

namespace hyp
{
    namespace
    {
        // 跟 Shader/ScreenProbe/ScreenProbeDiffuseIndirectComposite.slang 里的 ScreenProbeDiffuseIndirectCompositePushConstants 逐字段对应
        struct ScreenProbeDiffuseIndirectCompositePushConstants
        {
            uint2 viewRectMin;
        };
        static_assert(sizeof(ScreenProbeDiffuseIndirectCompositePushConstants) == 8);
    }

    struct ScreenProbeDiffuseIndirectCompositeData
    {
        uint32_t viewId;
        float4 viewport;
        ScreenProbeDiffuseIndirectCompositePushConstants constants;
        rg::TextureHandle sceneColor;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle gbufferD;
        rg::TextureHandle sceneDepthD;
        rg::TextureHandle diffuseIndirect;
        rg::TextureHandle shortRangeAO;
        rg::TextureHandle roughSpecularIndirect;
        rg::TextureHandle specularIndirect;
    };

    struct ScreenProbeDiffuseIndirectCompositeStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<11>> perViewCaches;
    };

    void ScreenProbeDiffuseIndirectCompositePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view
    )
    {
        const float4 viewport = view.getViewport();

        renderGraph.addGraphicsPass<ScreenProbeDiffuseIndirectCompositeData>(
            "ScreenProbeDiffuseIndirectComposite"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeDiffuseIndirectCompositeData& data)
            {
                data.viewId = view.getViewId();
                data.viewport = viewport;
                data.constants.viewRectMin = uint2(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));

                auto& blackboard = builder.getBlackboard();
                rg::TextureHandle sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Load);
                blackboard.put("SceneColor"_hs(data.viewId), data.sceneColor);
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(data.viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(data.viewId)));
                data.gbufferC = builder.sample(blackboard.get<rg::TextureHandle>("GBufferC"_hs(data.viewId)));
                data.gbufferD = builder.sample(blackboard.get<rg::TextureHandle>("GBufferD"_hs(data.viewId)));
                data.sceneDepthD = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(data.viewId)));

                data.diffuseIndirect = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeDiffuseIndirectFiltered"_hs(data.viewId)));
                data.shortRangeAO = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeShortRangeAOFiltered"_hs(data.viewId)));
                data.roughSpecularIndirect = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRoughSpecularIndirectFiltered"_hs(data.viewId)));
                data.specularIndirect = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionSpecularIndirect"_hs(data.viewId)));
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeDiffuseIndirectCompositeData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeDiffuseIndirectCompositePass, ScreenProbeDiffuseIndirectCompositeStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeDiffuseIndirectComposite",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(ScreenProbeDiffuseIndirectComposite, Vertex),
                            GET_PASS_SHADER_BYTECODE(ScreenProbeDiffuseIndirectComposite, Fragment)));
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeDiffuseIndirectComposite_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                    storage.perViewCaches.resize(data.viewId + 1u);
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* gbufferD = context.getTexture(data.gbufferD);
                const rhi::TextureView* sceneDepthD = context.getTexture(data.sceneDepthD);
                const rhi::TextureView* diffuseIndirect = context.getTexture(data.diffuseIndirect);
                const rhi::TextureView* shortRangeAO = context.getTexture(data.shortRangeAO);
                const rhi::TextureView* roughSpecularIndirect = context.getTexture(data.roughSpecularIndirect);
                const rhi::TextureView* specularIndirect = context.getTexture(data.specularIndirect);
                const rhi::TextureView* dfgLut = &renderBackend.getDefaultResources().getDfgLutView();
                rhi::Sampler* lutSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (bindGroupCache.needUpdate(
                        gbufferA, gbufferB, gbufferC, gbufferD, sceneDepthD, diffuseIndirect, shortRangeAO,
                        roughSpecularIndirect, specularIndirect, dfgLut, lutSampler))
                {
                    bindGroupCache.update(renderBackend, *storage.bindGroupLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeDiffuseIndirectComposite_Bindings);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("GbufferCImage", gbufferC);
                        writer.setTexture("GbufferDImage", gbufferD);
                        writer.setTexture("SceneDepthImage", sceneDepthD);
                        writer.setTexture("DiffuseIndirect", diffuseIndirect);
                        writer.setTexture("ShortRangeAOTexture", shortRangeAO);
                        writer.setTexture("RoughSpecularIndirect", roughSpecularIndirect);
                        writer.setTexture("SpecularIndirect", specularIndirect);
                        writer.setTexture("DfgLut", dfgLut);
                        writer.setSampler("LutSampler", lutSampler);
                        writer.apply(bindGroup);
                    });
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createPipeline(
                        rendererContext, storage.shader->getVariant(), bindGroupCache.getBindGroup()->getLayout());
                }

                setViewportAndScissor(cmdList, data.viewport);

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Graphics, &storage.pipeline->getLayout(), bindGroupCache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.draw(3, 1, 0, 0);
            });
    }

    rhi::Pipeline* ScreenProbeDiffuseIndirectCompositePass::createPipeline(
        RendererContext& rendererContext,
        ShaderVariant shaderVariant,
        const rhi::BindGroupLayout& bindGroupLayout
    )
    {
        auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::fullscreenTriangle();
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get<
            rhi::ColorBlendAttachmentState{ true, rhi::BlendFactor::One, rhi::BlendFactor::One, rhi::BlendOp::Add, rhi::BlendFactor::Zero, rhi::BlendFactor::One, rhi::BlendOp::Add, 0xF }
        >();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::Undefined,
            rhi::TextureFormat::RGBA16_SFLOAT
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
