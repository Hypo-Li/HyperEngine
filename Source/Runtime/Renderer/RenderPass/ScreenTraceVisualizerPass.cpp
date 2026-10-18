#include "ScreenTraceVisualizerPass.h"
#include "RenderPassUtils.h"
#include "HZBPass.h"
#include "../SkyLightProxy.h"

#include <RenderBase/RenderBackend.h>

#include <ScreenTraceVisualizer.generated.h>

namespace hyp
{
    namespace
    {
        // 跟 Shader/PostProcess/ScreenTraceVisualizer.slang 里的
        // ScreenTraceVisualizerPushConstants 逐字段对应
        struct ScreenTraceVisualizerPushConstants
        {
            uint32_t visualizeMode;
            uint32_t padding0;
            float2 hzbUvFactor;
            float2 hzbBaseTexelSize;
            float maxWorldTraceDistance;
            float padding1;
        };
        static_assert(sizeof(ScreenTraceVisualizerPushConstants) == 32);

        // UE Lumen::GetMaxTraceDistance() 出厂值 20000cm, 本引擎世界/view space 单位是米
        // (见 AtmosphereCommon.slang 调用点 distanceToFragmentKm = length(...) * 0.001 那一类
        // 换算), 换算成 200 米——跟 Document/Lumen_ScreenProbeGather_Plan.md §6 的
        // MaxTraceDistance 一致
        constexpr float kMaxWorldTraceDistanceMeters = 200.0f;

        rhi::Pipeline* createScreenTraceVisualizerPipeline(
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
            pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
                rhi::TextureFormat::Undefined,
                rhi::TextureFormat::RGBA16_SFLOAT
            >();
            pipelineKey.pipelineLayout = pipelineLayout;
            return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
        }
    }

    struct ScreenTraceVisualizerData
    {
        uint32_t viewId;
        float4 viewport;
        bool isRadianceMode;
        ScreenTraceVisualizerPushConstants constants;
        rg::TextureHandle sceneColorOutput;
        rg::TextureHandle sceneColorInput;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle gbufferD;
        rg::TextureHandle sceneDepthD;
        rg::TextureHandle sceneHZB;
        rg::TextureHandle skyLightPrefilter;
        rg::BufferHandle skyLightData;
    };

    struct ScreenTraceVisualizerStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<11>> perViewCaches;
    };

    void ScreenTraceVisualizerPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 extent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));
        const SceneVisualizeMode visualizeMode = scene.getSceneVisualizeMode();
        const bool isRadianceMode = visualizeMode == SceneVisualizeMode::ScreenTraceRadiance;

        // HZB UV 换算跟 AOPass::addSSAOToRenderGraph 的 screenToHZBUVScale 同一个公式——两者都是
        // "screenUV[0,1] -> HZB 贴图 UV[0,hzbUvFactor]"的缩放, 必须用同一份 HZBPass::getBaseExtent()
        // 才能跟 HZBPass 实际构建出的 mip0 尺寸对上
        const uint2 hzbBaseExtent = HZBPass::getBaseExtent(extent);
        const float2 hzbUvFactor(
            float(extent.x) / float(2u * hzbBaseExtent.x),
            float(extent.y) / float(2u * hzbBaseExtent.y));
        const float2 hzbBaseTexelSize(1.0f / float(hzbBaseExtent.x), 1.0f / float(hzbBaseExtent.y));

        // 天空辐射度兜底, 跟 RadiosityPass.cpp 同一份资源/同一种导入方式——没有活跃天光就绑黑色
        // cube, miss 等价于天空辐射度恒为 0, 不需要在 shader 端另开分支
        SkyLightProxy* activeSkyLight = scene.getActiveSkyLightProxy();
        TextureProxy* skyLightPrefilterProxy = activeSkyLight ? activeSkyLight->getSpecularPrefilter() : nullptr;
        rhi::TextureView& skyLightPrefilterView = skyLightPrefilterProxy
            ? skyLightPrefilterProxy->getTextureView()
            : rendererContext.getRenderBackend().getDefaultResources().getBlackTextureCubeView();
        rg::TextureHandle skyLightPrefilterHandle = renderGraph.importTexture(
            "ScreenTraceVisualizerActiveSkyLightPrefilter"_hs(scene.getSceneId()), &skyLightPrefilterView, rhi::ResourceState::Shader_Sampled);
        rhi::Buffer& skyLightDataBuffer = activeSkyLight
            ? activeSkyLight->getDataBuffer()
            : rendererContext.getRenderBackend().getDefaultResources().getDummyStorageBuffer();
        rg::BufferHandle skyLightDataHandle = renderGraph.importBuffer(
            "ScreenTraceVisualizerActiveSkyLightData"_hs(scene.getSceneId()), &skyLightDataBuffer);

        renderGraph.addGraphicsPass<ScreenTraceVisualizerData>(
            "ScreenTraceVisualizer"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenTraceVisualizerData& data)
            {
                data.viewId = viewId;
                data.viewport = viewport;
                data.isRadianceMode = isRadianceMode;
                data.constants.visualizeMode = static_cast<uint32_t>(visualizeMode);
                data.constants.hzbUvFactor = hzbUvFactor;
                data.constants.hzbBaseTexelSize = hzbBaseTexelSize;
                data.constants.maxWorldTraceDistance = kMaxWorldTraceDistanceMeters;
                data.constants.padding0 = 0u;
                data.constants.padding1 = 0.0f;

                auto& blackboard = builder.getBlackboard();

                // Radiance 模式在这里挂的时机是正常光照链路(含 TAA)跑完之后(见
                // RenderPipeline.cpp), "SceneColor" 这时候已经是这一帧真实的最终画面——采样后
                // 必须换一张新纹理写, 不能读写同一张附件, 跟 TAAPass 处理 "SceneColor" 同一个套路
                if (isRadianceMode)
                {
                    rg::TextureHandle prevSceneColor = blackboard.get<rg::TextureHandle>("SceneColor"_hs(viewId));
                    data.sceneColorInput = builder.sample(prevSceneColor);
                }

                rhi::TextureDesc sceneColorDesc;
                sceneColorDesc.type = rhi::TextureType::Texture_2D;
                sceneColorDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                sceneColorDesc.extent = { extent.x, extent.y, 1 };
                sceneColorDesc.mipCount = 1;
                sceneColorDesc.layerCount = 1;
                sceneColorDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto sceneColor = builder.createTexture("SceneColor"_hs(viewId), sceneColorDesc);
                data.sceneColorOutput = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Clear);
                blackboard.put("SceneColor"_hs(viewId), data.sceneColorOutput);

                rg::TextureHandle gbufferA = blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId));
                data.gbufferA = builder.sample(gbufferA);
                rg::TextureHandle gbufferB = blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId));
                data.gbufferB = builder.sample(gbufferB);
                rg::TextureHandle gbufferC = blackboard.get<rg::TextureHandle>("GBufferC"_hs(viewId));
                data.gbufferC = builder.sample(gbufferC);
                rg::TextureHandle gbufferD = blackboard.get<rg::TextureHandle>("GBufferD"_hs(viewId));
                data.gbufferD = builder.sample(gbufferD);
                rg::TextureHandle sceneDepthD = blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId));
                data.sceneDepthD = builder.sample(sceneDepthD);
                rg::TextureHandle sceneHZB = blackboard.get<rg::TextureHandle>("SceneHZB"_hs(viewId));
                data.sceneHZB = builder.sample(sceneHZB);
                data.skyLightPrefilter = builder.sample(skyLightPrefilterHandle);
                data.skyLightData = builder.accessBuffer(skyLightDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenTraceVisualizerData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenTraceVisualizerPass, ScreenTraceVisualizerStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenTraceVisualizer",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(ScreenTraceVisualizer, Vertex),
                            GET_PASS_SHADER_BYTECODE(ScreenTraceVisualizer, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kScreenTraceVisualizer_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                auto& defaultResources = renderBackend.getDefaultResources();
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* gbufferC = context.getTexture(data.gbufferC);
                const rhi::TextureView* gbufferD = context.getTexture(data.gbufferD);
                const rhi::TextureView* sceneDepthD = context.getTexture(data.sceneDepthD);
                const rhi::TextureView* sceneHZB = context.getTexture(data.sceneHZB);
                // Hit State 模式没有可采样的 "SceneColor" 输入(见 builder 阶段, 只有 Radiance
                // 模式才会 sample 它)——绑一张黑纹理占位, shader 那条分支反正不会执行到采样它
                const rhi::TextureView* sceneColorInput = data.isRadianceMode
                    ? context.getTexture(data.sceneColorInput)
                    : &defaultResources.getBlackTextureView();
                const rhi::TextureView* skyLightPrefilter = context.getTexture(data.skyLightPrefilter);
                rhi::Buffer* skyLightData = context.getBuffer(data.skyLightData);
                rhi::Sampler* pointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();
                rhi::Sampler* linearClampNoMipSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (bindGroupCache.needUpdate(gbufferA, gbufferB, gbufferC, gbufferD, sceneDepthD, sceneHZB, pointClampSampler, sceneColorInput, skyLightPrefilter, linearClampNoMipSampler, skyLightData))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kScreenTraceVisualizer_Bindings);
                            writer.setTexture("GbufferAImage", gbufferA);
                            writer.setTexture("GbufferBImage", gbufferB);
                            writer.setTexture("GbufferCImage", gbufferC);
                            writer.setTexture("GbufferDImage", gbufferD);
                            writer.setTexture("SceneDepthImage", sceneDepthD);
                            writer.setTexture("SceneHZBImage", sceneHZB);
                            writer.setSampler("PointClampSampler", pointClampSampler);
                            writer.setTexture("SceneColorInput", sceneColorInput);
                            writer.setSampler("LinearSampler", linearClampNoMipSampler);
                            writer.setTexture("SkyLightPrefilter", skyLightPrefilter);
                            writer.setBuffer("SkyLightData", skyLightData);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createScreenTraceVisualizerPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        *storage.bindGroupLayout
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
                    std::span<const uint8_t>(
                        reinterpret_cast<const uint8_t*>(&data.constants),
                        sizeof(data.constants))
                );

                cmdList.draw(3, 1, 0, 0);
            }
        );
    }
}
