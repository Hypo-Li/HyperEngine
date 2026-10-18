#include "ScreenProbeFilteringPass.h"
#include "RenderPassUtils.h"
#include "ScreenProbeConstants.h"
#include "ScreenProbePlacementPass.h"

#include <RenderBase/RenderBackend.h>

#include <ScreenProbeCompositeTraces.generated.h>
#include <ScreenProbeCalculateMoving.generated.h>
#include <ScreenProbeFilterGatherTraces.generated.h>
#include <ScreenProbeConvertToIrradiance.generated.h>
#include <ScreenProbeFixupBorders.generated.h>

namespace hyp
{
    namespace
    {
        // 跟 Shader/ScreenProbe/ScreenProbeCommon.slang 里的同名常量必须保持一致
        constexpr uint32_t kScreenProbeGatherOctahedronResolution = 8u;
        constexpr uint32_t kScreenProbeRadianceWithBorderResolution = 10u;
        constexpr uint32_t kSpatialFilterNumPasses = 3u;

        // UE r.Lumen.ScreenProbeGather.MaxRayIntensity 默认值(LumenScreenProbeFiltering.cpp:68)
        constexpr float kMaxRayIntensity = 10.0f;

        // 跟 Source/Runtime/Renderer/RenderPass/ScreenProbeTracePass.cpp 里的同名常量必须保持一致
        constexpr float kMaxWorldTraceDistanceMeters = 200.0f;

        // 跟 Shader/ScreenProbe/ScreenProbeCompositeTraces.slang 里的 ScreenProbeCompositeTracesPushConstants 逐字段对应
        struct ScreenProbeCompositeTracesPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
            float maxRayIntensity;
            float maxWorldTraceDistance;
        };
        static_assert(sizeof(ScreenProbeCompositeTracesPushConstants) == 36);

        // 跟 Shader/ScreenProbe/ScreenProbeCalculateMoving.slang 里的 ScreenProbeCalculateMovingPushConstants 逐字段对应
        struct ScreenProbeCalculateMovingPushConstants
        {
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
        };
        static_assert(sizeof(ScreenProbeCalculateMovingPushConstants) == 12);

        // 跟 Shader/ScreenProbe/ScreenProbeFilterGatherTraces.slang 里的 ScreenProbeFilterGatherTracesPushConstants 逐字段对应
        struct ScreenProbeFilterGatherTracesPushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            uint2 tileGridSize;
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
            float maxWorldTraceDistance;
        };
        static_assert(sizeof(ScreenProbeFilterGatherTracesPushConstants) == 40);

        // 跟 Shader/ScreenProbe/ScreenProbeConvertToIrradiance.slang 里的 ScreenProbeConvertToIrradiancePushConstants 逐字段对应
        struct ScreenProbeConvertToIrradiancePushConstants
        {
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
        };
        static_assert(sizeof(ScreenProbeConvertToIrradiancePushConstants) == 12);

        // 跟 Shader/ScreenProbe/ScreenProbeFixupBorders.slang 里的 ScreenProbeFixupBordersPushConstants 逐字段对应
        struct ScreenProbeFixupBordersPushConstants
        {
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
        };
        static_assert(sizeof(ScreenProbeFixupBordersPushConstants) == 12);
    }

    struct ScreenProbeCompositeTracesData
    {
        uint32_t viewId;
        ScreenProbeCompositeTracesPushConstants constants;
        rg::TextureHandle probeSceneDepth;
        rg::BufferHandle adaptiveProbePosAndTileIndices;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
        rg::TextureHandle traceRadiance;
        rg::TextureHandle traceHit;
        rg::TextureHandle blueNoise;
        rg::TextureHandle rayInfos;
        rg::TextureHandle probeRadiance;
        rg::TextureHandle probeHitDistance;
        rg::TextureHandle probeTraceMoving;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    struct ScreenProbeCalculateMovingData
    {
        uint32_t viewId;
        ScreenProbeCalculateMovingPushConstants constants;
        rg::TextureHandle probeSceneDepth;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
        rg::TextureHandle probeTraceMoving;
        rg::TextureHandle probeMoving;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    struct ScreenProbeFilterGatherTracesData
    {
        uint32_t viewId;
        uint32_t passIndex;
        ScreenProbeFilterGatherTracesPushConstants constants;
        rg::TextureHandle probeSceneDepth;
        rg::BufferHandle adaptiveProbePosAndTileIndices;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
        rg::TextureHandle probeRadianceSrc;
        rg::TextureHandle probeHitDistance;
        rg::TextureHandle probeMoving;
        rg::TextureHandle probeRadianceDst;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    struct ScreenProbeConvertToIrradianceData
    {
        uint32_t viewId;
        ScreenProbeConvertToIrradiancePushConstants constants;
        rg::TextureHandle probeSceneDepth;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
        rg::TextureHandle probeRadiance;
        rg::TextureHandle probeIrradiance;
        rg::TextureHandle probeRadianceSHAmbient;
        rg::TextureHandle probeRadianceSHDirectional;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    struct ScreenProbeFixupBordersData
    {
        uint32_t viewId;
        ScreenProbeFixupBordersPushConstants constants;
        rg::TextureHandle probeSceneDepth;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
        rg::TextureHandle probeRadiance;
        rg::TextureHandle probeRadianceWithBorder;
        const rhi::BindGroup* viewBindGroup;
        uint32_t viewDataDynamicOffset;
    };

    struct ScreenProbeFilteringStorage
    {
        std::shared_ptr<PassShader> compositeShader;
        TRef<rhi::BindGroupLayout> compositeLayout;
        TRef<rhi::Pipeline> compositePipeline;
        std::vector<BindGroupCache<10>> compositeCaches;

        std::shared_ptr<PassShader> movingShader;
        TRef<rhi::BindGroupLayout> movingLayout;
        TRef<rhi::Pipeline> movingPipeline;
        std::vector<BindGroupCache<4>> movingCaches;

        std::shared_ptr<PassShader> filterShader;
        TRef<rhi::BindGroupLayout> filterLayout;
        TRef<rhi::Pipeline> filterPipeline;
        // 每个 view 占 kSpatialFilterNumPasses 个连续槽位(下标 = viewId * kSpatialFilterNumPasses + passIndex)——
        // 3 遍 ping-pong 各自绑定不同的源/目标纹理, 不能共用同一个 cache 槽位
        std::vector<BindGroupCache<7>> filterCaches;

        std::shared_ptr<PassShader> irradianceShader;
        TRef<rhi::BindGroupLayout> irradianceLayout;
        TRef<rhi::Pipeline> irradiancePipeline;
        std::vector<BindGroupCache<3 + kScreenProbeIrradianceTextureCount>> irradianceCaches;

        std::shared_ptr<PassShader> fixupBordersShader;
        TRef<rhi::BindGroupLayout> fixupBordersLayout;
        TRef<rhi::Pipeline> fixupBordersPipeline;
        std::vector<BindGroupCache<4>> fixupBordersCaches;
    };

    void ScreenProbeFilteringPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view
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

        rg::TextureHandle blueNoiseHandle = renderGraph.importTexture(
            "ScreenProbeFilteringBlueNoiseVec2Texture"_hs,
            &rendererContext.getRenderBackend().getDefaultResources().getBlueNoiseVec2View(),
            rhi::ResourceState::Shader_Sampled);

        const uint32_t viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);
        const rhi::BindGroup* viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();

        rg::TextureHandle rawProbeRadianceHandle;

        renderGraph.addComputePass<ScreenProbeCompositeTracesData>(
            "ScreenProbeCompositeTraces"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeCompositeTracesData& data)
            {
                data.viewId = viewId;
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.atlasWidth = atlasViewSize.x;
                data.constants.numUniformProbes = numUniformProbes;
                data.constants.maxNumAdaptiveProbes = maxNumAdaptiveProbes;
                data.constants.maxRayIntensity = kMaxRayIntensity;
                data.constants.maxWorldTraceDistance = kMaxWorldTraceDistanceMeters;
                data.viewBindGroup = viewBindGroup;
                data.viewDataDynamicOffset = viewDataDynamicOffset;

                auto& blackboard = builder.getBlackboard();
                data.probeSceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeSceneDepth"_hs(viewId)));
                data.adaptiveProbePosAndTileIndices = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbePosAndTileIndices"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.adaptiveProbeTotalAndTileCounts = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.indirectArgs = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("ScreenProbeIndirectArgs"_hs(viewId)), rhi::BufferUsage::Indirect, rg::AccessType::Read_Only);
                data.traceRadiance = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeTraceRadiance"_hs(viewId)));
                data.traceHit = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeTraceHit"_hs(viewId)));
                data.blueNoise = builder.sample(blueNoiseHandle);
                data.rayInfos = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRayInfos"_hs(viewId)));

                rhi::TextureDesc radianceDesc;
                radianceDesc.type = rhi::TextureType::Texture_2D;
                radianceDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                radianceDesc.extent = {
                    atlasViewSize.x * kScreenProbeGatherOctahedronResolution,
                    atlasViewSize.y * kScreenProbeGatherOctahedronResolution,
                    1u };
                radianceDesc.mipCount = 1u;
                radianceDesc.layerCount = 1u;
                radianceDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto probeRadiance = builder.createTexture("ScreenProbeRadiance"_hs(viewId), radianceDesc);
                data.probeRadiance = builder.storage(probeRadiance, rg::AccessType::Write_Only);
                rawProbeRadianceHandle = data.probeRadiance;

                rhi::TextureDesc hitDistanceDesc = radianceDesc;
                hitDistanceDesc.format = rhi::TextureFormat::R8_UNORM;
                auto probeHitDistance = builder.createTexture("ScreenProbeHitDistance"_hs(viewId), hitDistanceDesc);
                data.probeHitDistance = builder.storage(probeHitDistance, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeHitDistance"_hs(viewId), data.probeHitDistance);

                auto probeTraceMoving = builder.createTexture("ScreenProbeTraceMoving"_hs(viewId), hitDistanceDesc);
                data.probeTraceMoving = builder.storage(probeTraceMoving, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeTraceMoving"_hs(viewId), data.probeTraceMoving);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeCompositeTracesData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeFilteringPass, ScreenProbeFilteringStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.compositeShader)
                {
                    storage.compositeShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeCompositeTraces",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeCompositeTraces, Compute)));
                    storage.compositeLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeCompositeTraces_Bindings);
                    storage.compositePipeline = createComputePipeline(rendererContext, storage.compositeShader->getVariant(), *storage.compositeLayout);
                }

                if (storage.compositeCaches.size() <= data.viewId)
                    storage.compositeCaches.resize(data.viewId + 1u);
                auto& cache = storage.compositeCaches[data.viewId];

                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                rhi::Buffer* adaptiveProbePosAndTileIndices = context.getBuffer(data.adaptiveProbePosAndTileIndices);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);
                const rhi::TextureView* traceRadiance = context.getTexture(data.traceRadiance);
                const rhi::TextureView* traceHit = context.getTexture(data.traceHit);
                const rhi::TextureView* blueNoise = context.getTexture(data.blueNoise);
                const rhi::TextureView* rayInfos = context.getTexture(data.rayInfos);
                rhi::TextureView* probeRadiance = context.getTexture(data.probeRadiance);
                rhi::TextureView* probeHitDistance = context.getTexture(data.probeHitDistance);
                rhi::TextureView* probeTraceMoving = context.getTexture(data.probeTraceMoving);

                if (cache.needUpdate(
                        probeSceneDepth, adaptiveProbePosAndTileIndices, adaptiveProbeTotalAndTileCounts, traceRadiance, traceHit, blueNoise, rayInfos,
                        probeRadiance, probeHitDistance, probeTraceMoving))
                {
                    cache.update(renderBackend, *storage.compositeLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeCompositeTraces_Bindings);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setBuffer("AdaptiveScreenProbePosAndTileIndices", adaptiveProbePosAndTileIndices);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.setTexture("TraceRadiance", traceRadiance);
                        writer.setTexture("TraceHit", traceHit);
                        writer.setTexture("BlueNoiseVec2Texture", blueNoise);
                        writer.setTexture("ScreenProbeRayInfos", rayInfos);
                        writer.setTexture("ScreenProbeRadiance", probeRadiance);
                        writer.setTexture("ScreenProbeHitDistance", probeHitDistance);
                        writer.setTexture("ScreenProbeTraceMoving", probeTraceMoving);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.compositePipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.compositePipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.compositePipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.compositePipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatchIndirect(indirectArgs, 0);
            });

        renderGraph.addComputePass<ScreenProbeCalculateMovingData>(
            "ScreenProbeCalculateMoving"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeCalculateMovingData& data)
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
                data.probeTraceMoving = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeTraceMoving"_hs(viewId)));

                rhi::TextureDesc movingDesc;
                movingDesc.type = rhi::TextureType::Texture_2D;
                movingDesc.format = rhi::TextureFormat::R8_UNORM;
                movingDesc.extent = { atlasViewSize.x, atlasViewSize.y, 1u };
                movingDesc.mipCount = 1u;
                movingDesc.layerCount = 1u;
                movingDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto probeMoving = builder.createTexture("ScreenProbeMoving"_hs(viewId), movingDesc);
                data.probeMoving = builder.storage(probeMoving, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeMoving"_hs(viewId), data.probeMoving);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeCalculateMovingData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeFilteringPass, ScreenProbeFilteringStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.movingShader)
                {
                    storage.movingShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeCalculateMoving",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeCalculateMoving, Compute)));
                    storage.movingLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeCalculateMoving_Bindings);
                    storage.movingPipeline = createComputePipeline(rendererContext, storage.movingShader->getVariant(), *storage.movingLayout);
                }

                if (storage.movingCaches.size() <= data.viewId)
                    storage.movingCaches.resize(data.viewId + 1u);
                auto& cache = storage.movingCaches[data.viewId];

                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);
                const rhi::TextureView* probeTraceMoving = context.getTexture(data.probeTraceMoving);
                rhi::TextureView* probeMoving = context.getTexture(data.probeMoving);

                if (cache.needUpdate(probeSceneDepth, adaptiveProbeTotalAndTileCounts, probeTraceMoving, probeMoving))
                {
                    cache.update(renderBackend, *storage.movingLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeCalculateMoving_Bindings);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.setTexture("ScreenProbeTraceMoving", probeTraceMoving);
                        writer.setTexture("ScreenProbeMoving", probeMoving);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.movingPipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.movingPipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.movingPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.movingPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatchIndirect(indirectArgs, 0);
            });

        // 空间滤波(M4): 3 遍 ping-pong, 输入是 CompositeTraces 的原始 ScreenProbeRadiance, 每遍在
        // 两张本函数局部持有的瞬时纹理之间来回写, 最终结果(奇数遍后落在 pingPongTextures[0])
        // 供下面的 ConvertToIrradiance 消费, 不发布到 blackboard——外部没有别的消费者
        rg::TextureHandle pingPongTextures[2];
        rg::TextureHandle filteredProbeRadianceHandle = rawProbeRadianceHandle;

        for (uint32_t passIndex = 0; passIndex < kSpatialFilterNumPasses; ++passIndex)
        {
            const uint32_t dstSlot = passIndex % 2u;
            const rg::TextureHandle srcHandle = filteredProbeRadianceHandle;

            renderGraph.addComputePass<ScreenProbeFilterGatherTracesData>(
                "ScreenProbeFilterGatherTraces"_hs(passIndex),
                [&, passIndex, dstSlot, srcHandle](rg::RenderGraph::Builder& builder, ScreenProbeFilterGatherTracesData& data)
                {
                    data.viewId = viewId;
                    data.passIndex = passIndex;
                    data.constants.viewRectMin = viewRectMin;
                    data.constants.viewExtent = viewExtent;
                    data.constants.tileGridSize = tileGridSize;
                    data.constants.atlasWidth = atlasViewSize.x;
                    data.constants.numUniformProbes = numUniformProbes;
                    data.constants.maxNumAdaptiveProbes = maxNumAdaptiveProbes;
                    data.constants.maxWorldTraceDistance = kMaxWorldTraceDistanceMeters;
                    data.viewBindGroup = viewBindGroup;
                    data.viewDataDynamicOffset = viewDataDynamicOffset;

                    auto& blackboard = builder.getBlackboard();
                    data.probeSceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeSceneDepth"_hs(viewId)));
                    data.adaptiveProbePosAndTileIndices = builder.accessBuffer(
                        blackboard.get<rg::BufferHandle>("AdaptiveScreenProbePosAndTileIndices"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.adaptiveProbeTotalAndTileCounts = builder.accessBuffer(
                        blackboard.get<rg::BufferHandle>("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.indirectArgs = builder.accessBuffer(
                        blackboard.get<rg::BufferHandle>("ScreenProbeIndirectArgs"_hs(viewId)), rhi::BufferUsage::Indirect, rg::AccessType::Read_Only);
                    data.probeRadianceSrc = builder.sample(srcHandle);
                    data.probeHitDistance = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeHitDistance"_hs(viewId)));
                    data.probeMoving = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeMoving"_hs(viewId)));

                    if (!pingPongTextures[dstSlot].isValid())
                    {
                        rhi::TextureDesc radianceDesc;
                        radianceDesc.type = rhi::TextureType::Texture_2D;
                        radianceDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                        radianceDesc.extent = {
                            atlasViewSize.x * kScreenProbeGatherOctahedronResolution,
                            atlasViewSize.y * kScreenProbeGatherOctahedronResolution,
                            1u };
                        radianceDesc.mipCount = 1u;
                        radianceDesc.layerCount = 1u;
                        radianceDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                        pingPongTextures[dstSlot] = builder.createTexture("ScreenProbeRadiancePingPong"_hs(viewId)(dstSlot), radianceDesc);
                    }
                    data.probeRadianceDst = builder.storage(pingPongTextures[dstSlot], rg::AccessType::Write_Only);
                    pingPongTextures[dstSlot] = data.probeRadianceDst;
                    filteredProbeRadianceHandle = data.probeRadianceDst;
                },
                [&rendererContext](rg::ExecuteContext& context, ScreenProbeFilterGatherTracesData& data)
                {
                    auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeFilteringPass, ScreenProbeFilteringStorage>();
                    auto& renderBackend = context.getRenderBackend();

                    if (!storage.filterShader)
                    {
                        storage.filterShader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "ScreenProbeFilterGatherTraces",
                            makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeFilterGatherTraces, Compute)));
                        storage.filterLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeFilterGatherTraces_Bindings);
                        storage.filterPipeline = createComputePipeline(rendererContext, storage.filterShader->getVariant(), *storage.filterLayout);
                    }

                    const uint32_t cacheIndex = data.viewId * kSpatialFilterNumPasses + data.passIndex;
                    if (storage.filterCaches.size() <= cacheIndex)
                        storage.filterCaches.resize(cacheIndex + 1u);
                    auto& cache = storage.filterCaches[cacheIndex];

                    const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                    rhi::Buffer* adaptiveProbePosAndTileIndices = context.getBuffer(data.adaptiveProbePosAndTileIndices);
                    rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                    rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);
                    const rhi::TextureView* probeRadianceSrc = context.getTexture(data.probeRadianceSrc);
                    const rhi::TextureView* probeHitDistance = context.getTexture(data.probeHitDistance);
                    const rhi::TextureView* probeMoving = context.getTexture(data.probeMoving);
                    rhi::TextureView* probeRadianceDst = context.getTexture(data.probeRadianceDst);

                    if (cache.needUpdate(
                            probeSceneDepth, adaptiveProbePosAndTileIndices, adaptiveProbeTotalAndTileCounts, probeRadianceSrc,
                            probeHitDistance, probeMoving, probeRadianceDst))
                    {
                        cache.update(renderBackend, *storage.filterLayout, [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kScreenProbeFilterGatherTraces_Bindings);
                            writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                            writer.setBuffer("AdaptiveScreenProbePosAndTileIndices", adaptiveProbePosAndTileIndices);
                            writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                            writer.setTexture("ScreenProbeRadiance", probeRadianceSrc);
                            writer.setTexture("ScreenProbeHitDistance", probeHitDistance);
                            writer.setTexture("ScreenProbeMoving", probeMoving);
                            writer.setTexture("ScreenProbeRadianceFiltered", probeRadianceDst);
                            writer.apply(bindGroup);
                        });
                    }

                    auto& cmdList = context.getCommandList();
                    cmdList.bindPipeline(storage.filterPipeline.get());
                    cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.filterPipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                    cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.filterPipeline->getLayout(), cache.getBindGroup(), {});
                    cmdList.setImmediateData(
                        &storage.filterPipeline->getLayout(), 0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                    cmdList.dispatchIndirect(indirectArgs, 0);
                });
        }

        renderGraph.getBlackboard().put("ScreenProbeRadianceFiltered"_hs(viewId), filteredProbeRadianceHandle);

        renderGraph.addComputePass<ScreenProbeConvertToIrradianceData>(
            "ScreenProbeConvertToIrradiance"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeConvertToIrradianceData& data)
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
                data.probeRadiance = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRadianceFiltered"_hs(viewId)));

                if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                {
                    rhi::TextureDesc ambientDesc;
                    ambientDesc.type = rhi::TextureType::Texture_2D;
                    ambientDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                    ambientDesc.extent = { atlasViewSize.x, atlasViewSize.y, 1u };
                    ambientDesc.mipCount = 1u;
                    ambientDesc.layerCount = 1u;
                    ambientDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                    auto ambient = builder.createTexture("ScreenProbeRadianceSHAmbient"_hs(viewId), ambientDesc);
                    data.probeRadianceSHAmbient = builder.storage(ambient, rg::AccessType::Write_Only);
                    blackboard.put("ScreenProbeRadianceSHAmbient"_hs(viewId), data.probeRadianceSHAmbient);

                    rhi::TextureDesc directionalDesc;
                    directionalDesc.type = rhi::TextureType::Texture_2D;
                    directionalDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    directionalDesc.extent = {
                        atlasViewSize.x * kScreenProbeRadianceSHDirectionalTexelsPerProbe,
                        atlasViewSize.y,
                        1u };
                    directionalDesc.mipCount = 1u;
                    directionalDesc.layerCount = 1u;
                    directionalDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                    auto directional = builder.createTexture("ScreenProbeRadianceSHDirectional"_hs(viewId), directionalDesc);
                    data.probeRadianceSHDirectional = builder.storage(directional, rg::AccessType::Write_Only);
                    blackboard.put("ScreenProbeRadianceSHDirectional"_hs(viewId), data.probeRadianceSHDirectional);
                }
                else
                {
                    rhi::TextureDesc irradianceDesc;
                    irradianceDesc.type = rhi::TextureType::Texture_2D;
                    irradianceDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                    irradianceDesc.extent = {
                        atlasViewSize.x * kScreenProbeIrradianceWithBorderResolution,
                        atlasViewSize.y * kScreenProbeIrradianceWithBorderResolution,
                        1u };
                    irradianceDesc.mipCount = 1u;
                    irradianceDesc.layerCount = 1u;
                    irradianceDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                    auto probeIrradiance = builder.createTexture("ScreenProbeIrradianceWithBorder"_hs(viewId), irradianceDesc);
                    data.probeIrradiance = builder.storage(probeIrradiance, rg::AccessType::Write_Only);
                    blackboard.put("ScreenProbeIrradianceWithBorder"_hs(viewId), data.probeIrradiance);
                }
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeConvertToIrradianceData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeFilteringPass, ScreenProbeFilteringStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.irradianceShader)
                {
                    storage.irradianceShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeConvertToIrradiance",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeConvertToIrradiance, Compute)));
                    storage.irradianceLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeConvertToIrradiance_Bindings);
                    storage.irradiancePipeline = createComputePipeline(rendererContext, storage.irradianceShader->getVariant(), *storage.irradianceLayout);
                }

                if (storage.irradianceCaches.size() <= data.viewId)
                    storage.irradianceCaches.resize(data.viewId + 1u);
                auto& cache = storage.irradianceCaches[data.viewId];

                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);
                const rhi::TextureView* probeRadiance = context.getTexture(data.probeRadiance);

                if constexpr (kScreenProbeIrradianceFormat == ScreenProbeIrradianceFormat::SH3)
                {
                    rhi::TextureView* ambient = context.getTexture(data.probeRadianceSHAmbient);
                    rhi::TextureView* directional = context.getTexture(data.probeRadianceSHDirectional);

                    if (cache.needUpdate(probeSceneDepth, adaptiveProbeTotalAndTileCounts, probeRadiance, ambient, directional))
                    {
                        cache.update(renderBackend, *storage.irradianceLayout, [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kScreenProbeConvertToIrradiance_Bindings);
                            writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                            writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                            writer.setTexture("ScreenProbeRadiance", probeRadiance);
                            writer.setTexture("ScreenProbeRadianceSHAmbient", ambient);
                            writer.setTexture("ScreenProbeRadianceSHDirectional", directional);
                            writer.apply(bindGroup);
                        });
                    }
                }
                else
                {
                    rhi::TextureView* probeIrradiance = context.getTexture(data.probeIrradiance);

                    if (cache.needUpdate(probeSceneDepth, adaptiveProbeTotalAndTileCounts, probeRadiance, probeIrradiance))
                    {
                        cache.update(renderBackend, *storage.irradianceLayout, [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kScreenProbeConvertToIrradiance_Bindings);
                            writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                            writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                            writer.setTexture("ScreenProbeRadiance", probeRadiance);
                            writer.setTexture("ScreenProbeIrradianceWithBorder", probeIrradiance);
                            writer.apply(bindGroup);
                        });
                    }
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.irradiancePipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.irradiancePipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.irradiancePipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.irradiancePipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatchIndirect(indirectArgs, 0);
            });

        renderGraph.addComputePass<ScreenProbeFixupBordersData>(
            "ScreenProbeFixupBorders"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeFixupBordersData& data)
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
                data.probeRadiance = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRadianceFiltered"_hs(viewId)));

                rhi::TextureDesc radianceWithBorderDesc;
                radianceWithBorderDesc.type = rhi::TextureType::Texture_2D;
                radianceWithBorderDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                radianceWithBorderDesc.extent = {
                    atlasViewSize.x * kScreenProbeRadianceWithBorderResolution,
                    atlasViewSize.y * kScreenProbeRadianceWithBorderResolution,
                    1u };
                radianceWithBorderDesc.mipCount = 1u;
                radianceWithBorderDesc.layerCount = 1u;
                radianceWithBorderDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto probeRadianceWithBorder = builder.createTexture("ScreenProbeRadianceWithBorder"_hs(viewId), radianceWithBorderDesc);
                data.probeRadianceWithBorder = builder.storage(probeRadianceWithBorder, rg::AccessType::Write_Only);
                blackboard.put("ScreenProbeRadianceWithBorder"_hs(viewId), data.probeRadianceWithBorder);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeFixupBordersData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeFilteringPass, ScreenProbeFilteringStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.fixupBordersShader)
                {
                    storage.fixupBordersShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeFixupBorders",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeFixupBorders, Compute)));
                    storage.fixupBordersLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeFixupBorders_Bindings);
                    storage.fixupBordersPipeline = createComputePipeline(rendererContext, storage.fixupBordersShader->getVariant(), *storage.fixupBordersLayout);
                }

                if (storage.fixupBordersCaches.size() <= data.viewId)
                    storage.fixupBordersCaches.resize(data.viewId + 1u);
                auto& cache = storage.fixupBordersCaches[data.viewId];

                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);
                const rhi::TextureView* probeRadiance = context.getTexture(data.probeRadiance);
                rhi::TextureView* probeRadianceWithBorder = context.getTexture(data.probeRadianceWithBorder);

                if (cache.needUpdate(probeSceneDepth, adaptiveProbeTotalAndTileCounts, probeRadiance, probeRadianceWithBorder))
                {
                    cache.update(renderBackend, *storage.fixupBordersLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeFixupBorders_Bindings);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.setTexture("ScreenProbeRadiance", probeRadiance);
                        writer.setTexture("ScreenProbeRadianceWithBorder", probeRadianceWithBorder);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.fixupBordersPipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.fixupBordersPipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.fixupBordersPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setImmediateData(
                    &storage.fixupBordersPipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatchIndirect(indirectArgs, 0);
            });
    }
}
