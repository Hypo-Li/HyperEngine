#include "ScreenProbeImportanceSamplingPass.h"
#include "RenderPassUtils.h"
#include "ScreenProbePlacementPass.h"

#include <RenderBase/RenderBackend.h>

#include <ScreenProbeBRDFPDF.generated.h>
#include <ScreenProbeLightingPDF.generated.h>
#include <ScreenProbeLightingHistoryCopy.generated.h>
#include <ScreenProbeGenerateRays.generated.h>

namespace hyp
{
    namespace
    {
        // 跟 Shader/ScreenProbe/ScreenProbeCommon.slang 里的同名常量必须保持一致
        constexpr uint32_t kNumSHCoefficients = 9u;
        constexpr uint32_t kScreenProbeGatherOctahedronResolution = 8u;

        // 跟 Shader/ScreenProbe/ScreenProbeBRDFPDF.slang 里的 ScreenProbeBRDFPDFPushConstants 逐字段对应
        struct ScreenProbeBRDFPDFPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
        };
        static_assert(sizeof(ScreenProbeBRDFPDFPushConstants) == 28);

        // 跟 Shader/ScreenProbe/ScreenProbeLightingPDF.slang 里的 ScreenProbeLightingPDFPushConstants 逐字段对应
        struct ScreenProbeLightingPDFPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint2 tileGridSize;
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
            uint32_t historyValid;
        };
        static_assert(sizeof(ScreenProbeLightingPDFPushConstants) == 40);

        // 跟 Shader/ScreenProbe/ScreenProbeGenerateRays.slang 里的 ScreenProbeGenerateRaysPushConstants 逐字段对应
        struct ScreenProbeGenerateRaysPushConstants
        {
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
        };
        static_assert(sizeof(ScreenProbeGenerateRaysPushConstants) == 12);
    }

    struct ScreenProbeBRDFPDFData
    {
        uint32_t viewId;
        ScreenProbeBRDFPDFPushConstants constants;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle probeSceneDepth;
        rg::BufferHandle adaptiveProbePosAndTileIndices;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
        rg::BufferHandle brdfPDFSH;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    struct ScreenProbeLightingPDFData
    {
        uint32_t viewId;
        ScreenProbeLightingPDFPushConstants constants;
        rg::TextureHandle probeSceneDepth;
        rg::TextureHandle probeWorldNormal;
        rg::TextureHandle velocity;
        rg::BufferHandle adaptiveProbePosAndTileIndices;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
        rg::TextureHandle historyRadiance;
        rg::TextureHandle historySceneDepth;
        rg::TextureHandle lightingPDF;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    struct ScreenProbeGenerateRaysData
    {
        uint32_t viewId;
        ScreenProbeGenerateRaysPushConstants constants;
        rg::TextureHandle probeSceneDepth;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
        rg::BufferHandle brdfPDFSH;
        rg::TextureHandle lightingPDF;
        rg::TextureHandle rayInfos;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    struct ScreenProbeLightingHistoryCopyData
    {
        uint32_t viewId;
        uint2 radianceExtent;
        rg::TextureHandle probeSceneDepth;
        rg::TextureHandle probeRadiance;
        rg::TextureHandle historyRadiance;
        rg::TextureHandle historySceneDepth;
    };

    struct ScreenProbeImportanceSamplingStorage
    {
        std::shared_ptr<PassShader> brdfPDFShader;
        TRef<rhi::BindGroupLayout> brdfPDFLayout;
        TRef<rhi::Pipeline> brdfPDFPipeline;
        std::vector<BindGroupCache<7>> brdfPDFCaches;

        std::shared_ptr<PassShader> lightingPDFShader;
        TRef<rhi::BindGroupLayout> lightingPDFLayout;
        TRef<rhi::Pipeline> lightingPDFPipeline;
        std::vector<BindGroupCache<8>> lightingPDFCaches;

        std::shared_ptr<PassShader> historyCopyShader;
        TRef<rhi::BindGroupLayout> historyCopyLayout;
        TRef<rhi::Pipeline> historyCopyPipeline;
        std::vector<BindGroupCache<4>> historyCopyCaches;

        std::shared_ptr<PassShader> generateRaysShader;
        TRef<rhi::BindGroupLayout> generateRaysLayout;
        TRef<rhi::Pipeline> generateRaysPipeline;
        std::vector<BindGroupCache<5>> generateRaysCaches;
    };

    void ScreenProbeImportanceSamplingPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        PostProcessState& postProcess
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewRectMin(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        const uint2 tileGridSize = ScreenProbePlacementPass::getTileGridSize(viewExtent);
        const uint2 atlasViewSize = ScreenProbePlacementPass::getAtlasViewSize(tileGridSize);
        const uint32_t numUniformProbes = tileGridSize.x * tileGridSize.y;
        const uint32_t maxNumAdaptiveProbes = ScreenProbePlacementPass::getMaxNumAdaptiveProbes(tileGridSize);

        const uint32_t viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);
        const rhi::BindGroup* viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();

        renderGraph.addComputePass<ScreenProbeBRDFPDFData>(
            "ScreenProbeBRDFPDF"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeBRDFPDFData& data)
            {
                data.viewId = viewId;
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.atlasWidth = atlasViewSize.x;
                data.constants.numUniformProbes = numUniformProbes;
                data.constants.maxNumAdaptiveProbes = maxNumAdaptiveProbes;
                data.viewBindGroup = viewBindGroup;
                data.viewDataDynamicOffset = viewDataDynamicOffset;

                auto& blackboard = builder.getBlackboard();
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.gbufferB = builder.sample(blackboard.get<rg::TextureHandle>("GBufferB"_hs(viewId)));
                data.probeSceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeSceneDepth"_hs(viewId)));
                data.adaptiveProbePosAndTileIndices = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbePosAndTileIndices"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.adaptiveProbeTotalAndTileCounts = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.indirectArgs = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("ScreenProbeIndirectArgs"_hs(viewId)), rhi::BufferUsage::Indirect, rg::AccessType::Read_Only);

                auto brdfPDFSH = builder.createBuffer(
                    "ScreenProbeBRDFPDFSH"_hs(viewId),
                    rhi::BufferDesc{ uint64_t(atlasViewSize.x) * atlasViewSize.y * kNumSHCoefficients * sizeof(float), rhi::BufferUsage::Storage });
                data.brdfPDFSH = builder.accessBuffer(brdfPDFSH, rhi::BufferUsage::Storage, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeBRDFPDFSH"_hs(viewId), data.brdfPDFSH);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeBRDFPDFData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeImportanceSamplingPass, ScreenProbeImportanceSamplingStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.brdfPDFShader)
                {
                    storage.brdfPDFShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeBRDFPDF",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeBRDFPDF, Compute)));
                    storage.brdfPDFLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeBRDFPDF_Bindings);
                    storage.brdfPDFPipeline = createComputePipeline(rendererContext, storage.brdfPDFShader->getVariant(), *storage.brdfPDFLayout);
                }

                if (storage.brdfPDFCaches.size() <= data.viewId)
                    storage.brdfPDFCaches.resize(data.viewId + 1u);
                auto& cache = storage.brdfPDFCaches[data.viewId];

                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* gbufferB = context.getTexture(data.gbufferB);
                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                rhi::Buffer* adaptiveProbePosAndTileIndices = context.getBuffer(data.adaptiveProbePosAndTileIndices);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);
                rhi::Buffer* brdfPDFSH = context.getBuffer(data.brdfPDFSH);

                if (cache.needUpdate(sceneDepth, gbufferA, gbufferB, probeSceneDepth, adaptiveProbePosAndTileIndices, adaptiveProbeTotalAndTileCounts, brdfPDFSH))
                {
                    cache.update(renderBackend, *storage.brdfPDFLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeBRDFPDF_Bindings);
                        writer.setTexture("SceneDepth", sceneDepth);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("GbufferBImage", gbufferB);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setBuffer("AdaptiveScreenProbePosAndTileIndices", adaptiveProbePosAndTileIndices);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.setBuffer("BRDFProbabilityDensityFunctionSH", brdfPDFSH);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.brdfPDFPipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.brdfPDFPipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.brdfPDFPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.brdfPDFPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatchIndirect(indirectArgs, 0);
            });

        // M7.2: 历史资源单缓冲, 按这一帧的图集尺寸 ensure(尺寸变了旧内容作废, 见
        // PostProcessState::ensureScreenProbeLightingHistory 注释); isScreenProbeLightingHistoryValid()
        // 必须在 ensure 之后读, 跟 ensureSceneDepthHistory/isSceneDepthHistoryValid 同一个调用顺序约定
        const uint2 lightingHistoryRadianceExtent(
            atlasViewSize.x * kScreenProbeGatherOctahedronResolution, atlasViewSize.y * kScreenProbeGatherOctahedronResolution);
        postProcess.ensureScreenProbeLightingHistory(lightingHistoryRadianceExtent, atlasViewSize);
        const bool lightingHistoryValid = postProcess.isScreenProbeLightingHistoryValid();

        rg::TextureHandle historyRadianceHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeLightingHistoryRadiance"_hs(viewId), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeLightingHistoryRadiance"_hs(viewId), &postProcess.getScreenProbeLightingHistoryRadianceView(), rhi::ResourceState::Shader_Sampled);
        });
        rg::TextureHandle historySceneDepthHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeLightingHistorySceneDepth"_hs(viewId), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeLightingHistorySceneDepth"_hs(viewId), &postProcess.getScreenProbeLightingHistorySceneDepthView(), rhi::ResourceState::Shader_Sampled);
        });

        renderGraph.addComputePass<ScreenProbeLightingPDFData>(
            "ScreenProbeLightingPDF"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeLightingPDFData& data)
            {
                data.viewId = viewId;
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.tileGridSize = tileGridSize;
                data.constants.atlasWidth = atlasViewSize.x;
                data.constants.numUniformProbes = numUniformProbes;
                data.constants.maxNumAdaptiveProbes = maxNumAdaptiveProbes;
                data.constants.historyValid = lightingHistoryValid ? 1u : 0u;
                data.viewBindGroup = viewBindGroup;
                data.viewDataDynamicOffset = viewDataDynamicOffset;

                auto& blackboard = builder.getBlackboard();
                data.probeSceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeSceneDepth"_hs(viewId)));
                data.probeWorldNormal = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeWorldNormal"_hs(viewId)));
                data.velocity = builder.sample(blackboard.get<rg::TextureHandle>("Velocity"_hs(viewId)));
                data.adaptiveProbePosAndTileIndices = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbePosAndTileIndices"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.adaptiveProbeTotalAndTileCounts = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.indirectArgs = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("ScreenProbeIndirectArgs"_hs(viewId)), rhi::BufferUsage::Indirect, rg::AccessType::Read_Only);
                data.historyRadiance = builder.sample(historyRadianceHandle);
                data.historySceneDepth = builder.sample(historySceneDepthHandle);

                rhi::TextureDesc pdfDesc;
                pdfDesc.type = rhi::TextureType::Texture_2D;
                pdfDesc.format = rhi::TextureFormat::R16_SFLOAT;
                pdfDesc.extent = { lightingHistoryRadianceExtent.x, lightingHistoryRadianceExtent.y, 1u };
                pdfDesc.mipCount = 1u;
                pdfDesc.layerCount = 1u;
                pdfDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto lightingPDF = builder.createTexture("ScreenProbeLightingPDF"_hs(viewId), pdfDesc);
                data.lightingPDF = builder.storage(lightingPDF, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeLightingPDF"_hs(viewId), data.lightingPDF);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeLightingPDFData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeImportanceSamplingPass, ScreenProbeImportanceSamplingStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.lightingPDFShader)
                {
                    storage.lightingPDFShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeLightingPDF",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeLightingPDF, Compute)));
                    storage.lightingPDFLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeLightingPDF_Bindings);
                    storage.lightingPDFPipeline = createComputePipeline(rendererContext, storage.lightingPDFShader->getVariant(), *storage.lightingPDFLayout);
                }

                if (storage.lightingPDFCaches.size() <= data.viewId)
                    storage.lightingPDFCaches.resize(data.viewId + 1u);
                auto& cache = storage.lightingPDFCaches[data.viewId];

                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                const rhi::TextureView* probeWorldNormal = context.getTexture(data.probeWorldNormal);
                const rhi::TextureView* velocity = context.getTexture(data.velocity);
                rhi::Buffer* adaptiveProbePosAndTileIndices = context.getBuffer(data.adaptiveProbePosAndTileIndices);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);
                const rhi::TextureView* historyRadiance = context.getTexture(data.historyRadiance);
                const rhi::TextureView* historySceneDepth = context.getTexture(data.historySceneDepth);
                rhi::TextureView* lightingPDF = context.getTexture(data.lightingPDF);

                if (cache.needUpdate(
                        probeSceneDepth, probeWorldNormal, velocity, adaptiveProbePosAndTileIndices, adaptiveProbeTotalAndTileCounts,
                        historyRadiance, historySceneDepth, lightingPDF))
                {
                    cache.update(renderBackend, *storage.lightingPDFLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeLightingPDF_Bindings);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setTexture("ScreenProbeWorldNormal", probeWorldNormal);
                        writer.setTexture("VelocityImage", velocity);
                        writer.setBuffer("AdaptiveScreenProbePosAndTileIndices", adaptiveProbePosAndTileIndices);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.setTexture("ScreenProbeLightingHistorySceneDepth", historySceneDepth);
                        writer.setTexture("ScreenProbeLightingHistoryRadiance", historyRadiance);
                        writer.setTexture("RWLightingProbabilityDensityFunction", lightingPDF);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.lightingPDFPipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.lightingPDFPipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.lightingPDFPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.lightingPDFPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatchIndirect(indirectArgs, 0);
            });

        // M7.3: 合并 BRDF PDF(M7.1)× Lighting PDF(M7.2), 产出重定向表, 留给 M7.4(Trace 接线)消费,
        // 见 Shader/ScreenProbe/ScreenProbeGenerateRays.slang 顶部注释
        renderGraph.addComputePass<ScreenProbeGenerateRaysData>(
            "ScreenProbeGenerateRays"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeGenerateRaysData& data)
            {
                data.viewId = viewId;
                data.constants.atlasWidth = atlasViewSize.x;
                data.constants.numUniformProbes = numUniformProbes;
                data.constants.maxNumAdaptiveProbes = maxNumAdaptiveProbes;
                data.viewBindGroup = viewBindGroup;
                data.viewDataDynamicOffset = viewDataDynamicOffset;

                auto& blackboard = builder.getBlackboard();
                data.probeSceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeSceneDepth"_hs(viewId)));
                data.adaptiveProbeTotalAndTileCounts = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.indirectArgs = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("ScreenProbeIndirectArgs"_hs(viewId)), rhi::BufferUsage::Indirect, rg::AccessType::Read_Only);
                data.brdfPDFSH = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("ScreenProbeBRDFPDFSH"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.lightingPDF = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeLightingPDF"_hs(viewId)));

                rhi::TextureDesc rayInfosDesc;
                rayInfosDesc.type = rhi::TextureType::Texture_2D;
                rayInfosDesc.format = rhi::TextureFormat::R32_UINT;
                rayInfosDesc.extent = { lightingHistoryRadianceExtent.x, lightingHistoryRadianceExtent.y, 1u };
                rayInfosDesc.mipCount = 1u;
                rayInfosDesc.layerCount = 1u;
                rayInfosDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto rayInfos = builder.createTexture("ScreenProbeRayInfos"_hs(viewId), rayInfosDesc);
                data.rayInfos = builder.storage(rayInfos, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeRayInfos"_hs(viewId), data.rayInfos);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeGenerateRaysData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeImportanceSamplingPass, ScreenProbeImportanceSamplingStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.generateRaysShader)
                {
                    storage.generateRaysShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeGenerateRays",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeGenerateRays, Compute)));
                    storage.generateRaysLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeGenerateRays_Bindings);
                    storage.generateRaysPipeline = createComputePipeline(rendererContext, storage.generateRaysShader->getVariant(), *storage.generateRaysLayout);
                }

                if (storage.generateRaysCaches.size() <= data.viewId)
                    storage.generateRaysCaches.resize(data.viewId + 1u);
                auto& cache = storage.generateRaysCaches[data.viewId];

                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);
                rhi::Buffer* brdfPDFSH = context.getBuffer(data.brdfPDFSH);
                const rhi::TextureView* lightingPDF = context.getTexture(data.lightingPDF);
                rhi::TextureView* rayInfos = context.getTexture(data.rayInfos);

                if (cache.needUpdate(probeSceneDepth, adaptiveProbeTotalAndTileCounts, brdfPDFSH, lightingPDF, rayInfos))
                {
                    cache.update(renderBackend, *storage.generateRaysLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeGenerateRays_Bindings);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.setBuffer("BRDFProbabilityDensityFunctionSH", brdfPDFSH);
                        writer.setTexture("ScreenProbeLightingPDF", lightingPDF);
                        writer.setTexture("RWStructuredImportanceSampledRayInfosForTracing", rayInfos);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.generateRaysPipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.generateRaysPipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.generateRaysPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.generateRaysPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatchIndirect(indirectArgs, 0);
            });
    }

    void ScreenProbeImportanceSamplingPass::addLightingHistoryCopyToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        PostProcessState& postProcess
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));
        const uint2 tileGridSize = ScreenProbePlacementPass::getTileGridSize(viewExtent);
        const uint2 atlasViewSize = ScreenProbePlacementPass::getAtlasViewSize(tileGridSize);
        const uint2 radianceExtent(
            atlasViewSize.x * kScreenProbeGatherOctahedronResolution, atlasViewSize.y * kScreenProbeGatherOctahedronResolution);

        rg::TextureHandle historyRadianceHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeLightingHistoryRadiance"_hs(viewId), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeLightingHistoryRadiance"_hs(viewId), &postProcess.getScreenProbeLightingHistoryRadianceView(), rhi::ResourceState::Shader_Sampled);
        });
        rg::TextureHandle historySceneDepthHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeLightingHistorySceneDepth"_hs(viewId), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeLightingHistorySceneDepth"_hs(viewId), &postProcess.getScreenProbeLightingHistorySceneDepthView(), rhi::ResourceState::Shader_Sampled);
        });

        renderGraph.addComputePass<ScreenProbeLightingHistoryCopyData>(
            "ScreenProbeLightingHistoryCopy"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeLightingHistoryCopyData& data)
            {
                data.viewId = viewId;
                data.radianceExtent = radianceExtent;

                auto& blackboard = builder.getBlackboard();
                data.probeSceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeSceneDepth"_hs(viewId)));
                data.probeRadiance = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRadianceFiltered"_hs(viewId)));
                data.historyRadiance = builder.storage(historyRadianceHandle, rg::AccessType::Write_Only);
                data.historySceneDepth = builder.storage(historySceneDepthHandle, rg::AccessType::Write_Only);
                builder.markSideEffect();
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeLightingHistoryCopyData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeImportanceSamplingPass, ScreenProbeImportanceSamplingStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.historyCopyShader)
                {
                    storage.historyCopyShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeLightingHistoryCopy",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeLightingHistoryCopy, Compute)));
                    storage.historyCopyLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeLightingHistoryCopy_Bindings);
                    storage.historyCopyPipeline = createComputePipeline(rendererContext, storage.historyCopyShader->getVariant(), *storage.historyCopyLayout);
                }

                if (storage.historyCopyCaches.size() <= data.viewId)
                    storage.historyCopyCaches.resize(data.viewId + 1u);
                auto& cache = storage.historyCopyCaches[data.viewId];

                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                const rhi::TextureView* probeRadiance = context.getTexture(data.probeRadiance);
                rhi::TextureView* historyRadiance = context.getTexture(data.historyRadiance);
                rhi::TextureView* historySceneDepth = context.getTexture(data.historySceneDepth);

                if (cache.needUpdate(probeSceneDepth, probeRadiance, historyRadiance, historySceneDepth))
                {
                    cache.update(renderBackend, *storage.historyCopyLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeLightingHistoryCopy_Bindings);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setTexture("ScreenProbeRadiance", probeRadiance);
                        writer.setTexture("ScreenProbeLightingHistorySceneDepth", historySceneDepth);
                        writer.setTexture("ScreenProbeLightingHistoryRadiance", historyRadiance);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.historyCopyPipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.historyCopyPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.dispatch((data.radianceExtent.x + 7u) / 8u, (data.radianceExtent.y + 7u) / 8u, 1u);
            });

        postProcess.markScreenProbeLightingHistoryWritten();
    }
}
