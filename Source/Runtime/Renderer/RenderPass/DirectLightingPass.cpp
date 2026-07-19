#include "DirectLightingPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBackend.h>

#include <DirectLighting.generated.h>

#include <vector>

namespace hyp
{
    struct DirectLightingData
    {
        const SceneProxy* scene;
        uint32_t sceneId;
        uint32_t viewId;
        float4 viewport;
        bool hasAtmosphere;
        rg::TextureHandle sceneColor;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle sceneDepthD;
        rg::TextureHandle transmittanceLut;
    };

    struct DirectLightingStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        std::vector<BindGroupCache<7>> perViewCaches;
        TRef<rhi::Pipeline> pipeline;
    };

    void DirectLightingPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const AtmosphereProxy* atmosphere
    )
    {
        renderGraph.addGraphicsPass<DirectLightingData>(
            "DirectLighting"_hs,
            [&](rg::RenderGraph::Builder& builder, DirectLightingData& data)
            {
                data.scene = &scene;
                data.sceneId = scene.getSceneId();
                data.viewId = view.getViewId();
                data.viewport = view.getViewport();
                data.hasAtmosphere = atmosphere != nullptr;
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

                if (data.hasAtmosphere)
                {
                    auto transmittanceLut = blackboard.get<rg::TextureHandle>("TransmittanceLut"_hs(data.sceneId));
                    data.transmittanceLut = builder.sample(transmittanceLut);
                }
            },
            [&rendererContext, atmosphere](rg::ExecuteContext& context, DirectLightingData& data)
            {
                static auto& storage = rendererContext.getPassStorage().getOrCreate<DirectLightingPass, DirectLightingStorage>();

                auto& renderBackend = context.getRenderBackend();

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "DirectLighting",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(DirectLighting, Vertex),
                            GET_PASS_SHADER_BYTECODE(DirectLighting, Fragment)
                        )
                    );
                }

                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* sceneDepthD = context.getTexture(data.sceneDepthD);

                // 场景没有大气时绑 DefaultResources 的通用占位资源(全零 buffer + 全白纹理)。
                // DirectLighting.slang 里靠 topRadiusKm <= bottomRadiusKm(全零时天然成立)判断
                // 出"没有真实大气", 整段透过率计算直接跳过、transmittance 恒为 1, 不依赖占位 buffer
                // 里有什么具体内容

                auto& defaultResources = renderBackend.getDefaultResources();
                rhi::Buffer* atmosphereParameters = data.hasAtmosphere
                    ? &atmosphere->getParametersBuffer()
                    : &defaultResources.getDummyUniformBuffer();
                const rhi::TextureView* transmittanceLut = data.hasAtmosphere
                    ? context.getTexture(data.transmittanceLut)
                    : &defaultResources.getWhiteTextureView();
                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (bindGroupCache.needUpdate(gbufferA, gbufferB, gbufferC, sceneDepthD, atmosphereParameters, transmittanceLut, linearClampNoMipSampler))
                {
                    if (!storage.bindGroupLayout)
                    {
                        storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kDirectLighting_Bindings);
                    }

                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kDirectLighting_Bindings);
                            writer.setTexture("GbufferAImage", gbufferA);
                            writer.setTexture("GbufferBImage", gbufferB);
                            writer.setTexture("GbufferCImage", gbufferC);
                            writer.setTexture("SceneDepthImage", sceneDepthD);
                            writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.setTexture("TransmittanceLutTexture", transmittanceLut);
                            writer.setSampler("LutSampler", linearClampNoMipSampler);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
                    );
                }

                auto& cmd = context.getCommandList();

                context.beginPass();

                setViewportAndScissor(cmd, data.viewport);

                cmd.bindPipeline(storage.pipeline.get());
                cmd.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );

                for (auto directionalLightProxy : data.scene->getDirectionalLightProxies())
                {
                    uint32_t lightIndex = directionalLightProxy->getLightIndex();
                    cmd.setImmediateData(&storage.pipeline->getLayout(), 0, std::span((const uint8_t*)&lightIndex, sizeof(lightIndex)));
                    cmd.draw(3, 1, 0, 0);
                }
            },
            rg::PassFlags::Skip_Begin_Pass
        );
    }

    rhi::Pipeline* DirectLightingPass::createPipeline(
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
