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
        rg::TextureHandle gbufferD;
        rg::TextureHandle sceneDepthD;
        rg::TextureHandle skyLightPrefilter;
        rg::BufferHandle skyLightData;
        rg::TextureHandle ambientOcclusion;
        bool hasAmbientOcclusion;
        uint32_t applyDiffuse;
    };

    struct IndirectLightingStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<9>> perViewCaches;
    };

    void IndirectLightingPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        SkyLightProxy& skyLight,
        bool applyDiffuse
    )
    {
        // SpecularPrefilter/DataBuffer 都是常驻在 SkyLightProxy 上的资源(不是只在 capture
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
        auto skyLightDataHandle = renderGraph.importBuffer(
            "ActiveSkyLightData"_hs(scene.getSceneId()),
            &skyLight.getDataBuffer()
        );

        renderGraph.addGraphicsPass<IndirectLightingData>(
            "SkyLighting"_hs,
            [&](rg::RenderGraph::Builder& builder, IndirectLightingData& data)
            {
                data.sceneId = scene.getSceneId();
                data.applyDiffuse = applyDiffuse ? 1u : 0u;
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
                rg::TextureHandle gbufferD = blackboard.get<rg::TextureHandle>("GBufferD"_hs(data.viewId));
                data.gbufferD = builder.sample(gbufferD);
                rg::TextureHandle sceneDepthD = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(data.viewId));
                data.sceneDepthD = builder.sample(sceneDepthD);

                // AOPass 只在 ambientOcclusion.enabled 时才会往 blackboard 发布这个 key——关闭时
                // isValid() 为 false, execute 阶段回退到白纹理(不遮蔽), 等价于没有屏幕空间 AO,
                // 不需要额外的 shader 分支
                rg::TextureHandle ambientOcclusion = blackboard.get<rg::TextureHandle>("AmbientOcclusion"_hs(data.viewId));
                data.hasAmbientOcclusion = ambientOcclusion.isValid();
                if (data.hasAmbientOcclusion)
                {
                    data.ambientOcclusion = builder.sample(ambientOcclusion);
                }

                data.skyLightPrefilter = builder.sample(skyLightPrefilterHandle);
                data.skyLightData = builder.accessBuffer(
                    skyLightDataHandle,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Read_Only
                );
            },
            [&rendererContext](rg::ExecuteContext& context, IndirectLightingData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<IndirectLightingPass, IndirectLightingStorage>();

                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

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
                const rhi::TextureView* gbufferD = context.getTexture(data.gbufferD);
                const rhi::TextureView* sceneDepthD = context.getTexture(data.sceneDepthD);
                const rhi::TextureView* skyLightPrefilter = context.getTexture(data.skyLightPrefilter);
                rhi::Buffer* skyLightData = context.getBuffer(data.skyLightData);
                rhi::TextureView* dfgLutView = &renderBackend.getDefaultResources().getDfgLutView();
                rhi::TextureView* ambientOcclusion = data.hasAmbientOcclusion
                    ? context.getTexture(data.ambientOcclusion)
                    : &renderBackend.getDefaultResources().getWhiteTextureView();

                if (bindGroupCache.needUpdate(gbufferA, gbufferB, gbufferC, gbufferD, sceneDepthD, skyLightPrefilter, skyLightData, dfgLutView, ambientOcclusion))
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
                            writer.setTexture("GbufferDImage", gbufferD);
                            writer.setTexture("SceneDepthImage", sceneDepthD);
                            writer.setTexture("SkyLightSpecularPrefilter", skyLightPrefilter);
                            writer.setBuffer("SkyLightData", skyLightData);
                            writer.setTexture("DfgLut", dfgLutView);
                            writer.setSampler("LutSampler", linearNoMipSampler);
                            writer.setTexture("AmbientOcclusionImage", ambientOcclusion);
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

                setViewportAndScissor(cmdList, data.viewport);

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.applyDiffuse), sizeof(data.applyDiffuse))
                );

                cmdList.draw(3, 1, 0, 0);
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
