#include "IndirectLightingPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <SkyLighting.generated.h>

#include <vector>

namespace hyp
{
    struct IndirectLightingData
    {
        uint32_t sceneId;
        uint32_t viewId;
        float4 viewport;
        rg::TextureHandle sceneColor;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle sceneDepthD;
        rg::TextureHandle skyLightPrefilter;
        rg::BufferHandle skyLightPackedSHCoeff;
    };

    struct IndirectLightingStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<7>> perViewCaches;
    };

    void IndirectLightingPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        SkyLightProxy& skyLight
    )
    {
        // SpecularPrefilter/PackedSHBuffer 都是常驻在 SkyLightProxy 上的资源(不是只在 capture
        // 帧才存在的瞬时 render graph 资源), 直接从 proxy import 进来, 不走 blackboard——这样
        // 没有新 capture 发生的普通帧也能正常读到上一次 capture 算出来的结果
        TextureProxy* specularPrefilter = skyLight.getSpecularPrefilter();
        if (!specularPrefilter)
            return;

        auto skyLightPrefilterHandle = renderGraph.importTexture(
            "ActiveSkyLightPrefilter"_hs(scene.getSceneId()),
            &specularPrefilter->getTextureView(),
            rhi::ResourceState::Shader_Sampled
        );
        auto skyLightPackedSHCoeffHandle = renderGraph.importBuffer(
            "ActiveSkyLightPackedSHCoeff"_hs(scene.getSceneId()),
            &skyLight.getPackedSHBuffer()
        );

        renderGraph.addGraphicsPass<IndirectLightingData>(
            "SkyLighting"_hs,
            [&](rg::RenderGraph::Builder& builder, IndirectLightingData& data)
            {
                data.sceneId = scene.getSceneId();
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();

                auto& blackboard = builder.getBlackboard();
                rg::TextureHandle sceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(data.viewId));
                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Load);
                blackboard.put("SceneColor"_hs(data.viewId), data.sceneColor);
                rg::TextureHandle gbufferA = blackboard.get<rg::TextureHandle>("GBufferA"_hs(data.viewId));
                data.gbufferA = builder.sample(gbufferA);
                rg::TextureHandle gbufferB = blackboard.get<rg::TextureHandle>("GBufferB"_hs(data.viewId));
                data.gbufferB = builder.sample(gbufferB);
                rg::TextureHandle gbufferC = blackboard.get<rg::TextureHandle>("GBufferC"_hs(data.viewId));
                data.gbufferC = builder.sample(gbufferC);
                rg::TextureHandle sceneDepthD = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(data.viewId));
                data.sceneDepthD = builder.sample(sceneDepthD);

                data.skyLightPrefilter = builder.sample(skyLightPrefilterHandle);
                data.skyLightPackedSHCoeff = builder.accessBuffer(
                    skyLightPackedSHCoeffHandle,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Read_Only
                );
            },
            [&rendererContext](rg::ExecuteContext& context, IndirectLightingData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<IndirectLightingPass, IndirectLightingStorage>();

                auto& renderBackend = context.getRenderBackend();
                auto& cmd = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "SkyLighting",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(SkyLighting, Vertex),
                            GET_PASS_SHADER_BYTECODE(SkyLighting, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    // kSkyLighting_Bindings 是从 SkyLighting.slang 的 [[vk::binding(N, 2)]]
                    // 反射生成的, 不需要在这里再手写一遍 entries[]
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kSkyLighting_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* sceneDepthD = context.getTexture(data.sceneDepthD);
                const rhi::TextureView* skyLightPrefilter = context.getTexture(data.skyLightPrefilter);
                rhi::Buffer* skyLightPackedSHCoeff = context.getBuffer(data.skyLightPackedSHCoeff);
                rhi::TextureView* dfgLutView = &renderBackend.getDefaultResources().getDfgLutView();

                if (bindGroupCache.needUpdate(gbufferA, gbufferB, gbufferC, sceneDepthD, skyLightPrefilter, skyLightPackedSHCoeff, dfgLutView))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            rhi::Sampler* linearSampler = renderBackend.getSamplerCache().getLinearClampMip();
                            rhi::Sampler* linearNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();
                            PassBindGroupWriter writer(generated::kSkyLighting_Bindings);
                            writer.setSampler("LinearSampler", linearSampler);
                            writer.setTexture("GbufferAImage", gbufferA);
                            writer.setTexture("GbufferBImage", gbufferB);
                            writer.setTexture("GbufferCImage", gbufferC);
                            writer.setTexture("SceneDepthImage", sceneDepthD);
                            writer.setTexture("SkyLightSpecularPrefilter", skyLightPrefilter);
                            writer.setBuffer("SkyLightPackedSHCoeff", skyLightPackedSHCoeff);
                            writer.setTexture("DfgLut", dfgLutView);
                            writer.setSampler("LutSampler", linearNoMipSampler);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        bindGroupCache.getBindGroup()->getLayout()
                    );
                }

                setViewportAndScissor(cmd, data.viewport);

                cmd.bindPipeline(storage.pipeline.get());
                cmd.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );

                cmd.draw(3, 1, 0, 0);
            }
        );
    }

    rhi::Pipeline* IndirectLightingPass::createPipeline(
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
