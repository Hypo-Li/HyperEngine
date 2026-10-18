#include "ScreenProbeTracePass.h"
#include "RenderPassUtils.h"
#include "ScreenProbePlacementPass.h"
#include "HZBPass.h"
#include "../SkyLightProxy.h"
#include "../CameraProxy.h"
#include "../GlobalDistanceField.h"
#include "../LumenScene.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>

#include <ScreenProbeTrace.generated.h>
#include <ScreenProbeSceneDepthHistoryCopy.generated.h>

namespace hyp
{
    namespace
    {
        // 跟 Shader/ScreenProbe/ScreenProbeCommon.slang 里的同名常量必须保持一致
        constexpr uint32_t kScreenProbeTracingOctahedronResolution = 8u;

        // UE Lumen::GetMaxTraceDistance() 出厂值 20000cm, 本引擎单位是米, 见
        // Document/Lumen_ScreenProbeGather_Plan.md §6
        constexpr float kMaxWorldTraceDistanceMeters = 200.0f;

        // 跟 Shader/ScreenProbe/ScreenProbeTrace.slang 里的 ScreenProbeTracePushConstants 逐字段
        // 对应, 布尔标志压进 traceFlags 低 3 位——见该文件顶部注释, push constant 卡在
        // rhi::kImmediateDataSize(64 字节硬上限)
        struct ScreenProbeTracePushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            float2 hzbUvFactor;
            float2 hzbBaseTexelSize;
            uint32_t atlasWidth;
            uint32_t numUniformProbes;
            uint32_t maxNumAdaptiveProbes;
            uint32_t traceFlags;
            float maxWorldTraceDistance;
            int32_t gdfAtlasBrickGridExtent;
            int32_t mdfAtlasBrickGridExtent;
        };
        static_assert(sizeof(ScreenProbeTracePushConstants) == 60);

        constexpr uint32_t kScreenProbeTraceFlagScreenTraceEnabled = 1u << 0;
        constexpr uint32_t kScreenProbeTraceFlagDepthHistoryValid = 1u << 1;
        constexpr uint32_t kScreenProbeTraceFlagGdfTraceEnabled = 1u << 2;
    }

    struct ScreenProbeTraceData
    {
        uint32_t viewId;
        ScreenProbeTracePushConstants constants;
        const rhi::BindGroup* viewBindGroup;
        const rhi::BindGroup* instanceDataBindGroup;
        uint32_t viewDataDynamicOffset;
        bool useTAAHistory;
        bool gdfEnabled;
        rg::BufferHandle meshShapeData;
        rg::BufferHandle meshInstanceData;
        rg::BufferHandle gdfIndirection;
        rg::TextureHandle gdfAtlas;
        rg::BufferHandle objectGrid;
        rg::BufferHandle cardPages;
        rg::TextureHandle mdfBrickAtlas;
        rg::TextureHandle cardNormalAtlas;
        rg::TextureHandle cardDepthAtlas;
        rg::TextureHandle cardFinalLightingAtlas;
        rg::TextureHandle cardAlbedoAtlas;
        rg::TextureHandle cardEmissiveAtlas;
        rg::TextureHandle cardDirectLightingAtlas;
        rg::TextureHandle cardIndirectLightingAtlas;
        rg::TextureHandle probeSceneDepth;
        rg::TextureHandle probeWorldNormal;
        rg::TextureHandle probeWorldSpeed;
        rg::TextureHandle rayInfos;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle sceneHZB;
        rg::TextureHandle velocity;
        rg::TextureHandle taaHistory;
        rg::TextureHandle sceneDepthHistory;
        rg::TextureHandle skyLightPrefilter;
        rg::BufferHandle skyLightData;
        rg::TextureHandle blueNoise;
        rg::TextureHandle traceRadiance;
        rg::TextureHandle traceHit;
        rg::BufferHandle adaptiveProbePosAndTileIndices;
        rg::BufferHandle adaptiveProbeTotalAndTileCounts;
        rg::BufferHandle indirectArgs;
    };

    struct ScreenProbeSceneDepthHistoryCopyData
    {
        uint32_t viewId;
        uint2 extent;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle sceneDepthHistory;
    };

    struct ScreenProbeTraceStorage
    {
        std::shared_ptr<PassShader> traceShader;
        TRef<rhi::BindGroupLayout> traceLayout;
        TRef<rhi::Pipeline> tracePipeline;
        std::vector<BindGroupCache<32>> traceCaches;
        // GDF 追踪没启用时的占位, 只为让 bind group 完整
        TRef<rhi::Buffer> dummyStorageBuffer;

        std::shared_ptr<PassShader> copyShader;
        TRef<rhi::BindGroupLayout> copyLayout;
        TRef<rhi::Pipeline> copyPipeline;
        std::vector<BindGroupCache<2>> copyCaches;
    };

    void ScreenProbeTracePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const CameraProxy& camera,
        PostProcessState& postProcess,
        const LumenSceneUpdateResult& lumenUpdate
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewRectMin(static_cast<uint32_t>(viewport.x), static_cast<uint32_t>(viewport.y));
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        const uint2 tileGridSize = ScreenProbePlacementPass::getTileGridSize(viewExtent);
        const uint2 atlasViewSize = ScreenProbePlacementPass::getAtlasViewSize(tileGridSize);

        const uint2 hzbBaseExtent = HZBPass::getBaseExtent(viewExtent);
        const float2 hzbUvFactor(
            float(viewExtent.x) / float(2u * hzbBaseExtent.x),
            float(viewExtent.y) / float(2u * hzbBaseExtent.y));
        const float2 hzbBaseTexelSize(1.0f / float(hzbBaseExtent.x), 1.0f / float(hzbBaseExtent.y));

        postProcess.ensureSceneDepthHistory(viewExtent);

        // 屏幕命中读的是 TAA history, 没开 TAA 或历史无效时整段屏幕追踪都跳过
        bool useTAAHistory = false;
        if (postProcess.isJitterEnabled())
        {
            postProcess.ensureTAAHistory(viewExtent);
            useTAAHistory = postProcess.isTAAHistoryValid();
        }
        const bool depthHistoryValid = postProcess.isSceneDepthHistoryValid();
        const bool screenTraceEnabled = useTAAHistory && !scene.getDisableScreenTrace();

        // getOrImportResource: ScreenProbeTemporalFilterPass(M6)复用同一份句柄读历史深度做 disocclusion 测试
        rg::TextureHandle sceneDepthHistoryHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeSceneDepthHistory"_hs(viewId), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeSceneDepthHistory"_hs(viewId), &postProcess.getSceneDepthHistoryView(), rhi::ResourceState::Shader_Sampled);
        });
        rg::TextureHandle taaHistoryHandle;
        if (useTAAHistory)
        {
            taaHistoryHandle = renderGraph.importTexture(
                "ScreenProbeTraceTAAHistory"_hs(viewId), &postProcess.getTAAHistoryReadView(), rhi::ResourceState::Shader_Sampled);
        }

        SkyLightProxy* activeSkyLight = scene.getActiveSkyLightProxy();
        TextureProxy* skyLightPrefilterProxy = activeSkyLight ? activeSkyLight->getSpecularPrefilter() : nullptr;
        rhi::TextureView& skyLightPrefilterView = skyLightPrefilterProxy
            ? skyLightPrefilterProxy->getTextureView()
            : rendererContext.getRenderBackend().getDefaultResources().getBlackTextureCubeView();
        rg::TextureHandle skyLightPrefilterHandle = renderGraph.importTexture(
            "ScreenProbeTraceActiveSkyLightPrefilter"_hs(scene.getSceneId()), &skyLightPrefilterView, rhi::ResourceState::Shader_Sampled);
        rhi::Buffer& skyLightDataBuffer = activeSkyLight
            ? activeSkyLight->getDataBuffer()
            : rendererContext.getRenderBackend().getDefaultResources().getDummyStorageBuffer();
        rg::BufferHandle skyLightDataHandle = renderGraph.importBuffer(
            "ScreenProbeTraceActiveSkyLightData"_hs(scene.getSceneId()), &skyLightDataBuffer);

        rg::TextureHandle blueNoiseHandle = renderGraph.importTexture(
            "BlueNoiseVec2Texture"_hs, &rendererContext.getRenderBackend().getDefaultResources().getBlueNoiseVec2View(), rhi::ResourceState::Shader_Sampled);

        // 同一个 key 走 getOrImportResource, 复用 GDF 合成 pass 已经导入过的句柄
        const bool gdfEnabled = lumenUpdate.ready;
        int32_t gdfAtlasBrickGridExtent = 0;
        int32_t mdfAtlasBrickGridExtent = 0;
        rg::BufferHandle meshShapeDataHandle;
        rg::BufferHandle meshInstanceDataHandle;
        rg::BufferHandle gdfIndirectionHandle;
        rg::BufferHandle gdfObjectGridHandle;
        rg::TextureHandle gdfAtlasHandle;
        rg::TextureHandle mdfBrickAtlasHandle;
        if (gdfEnabled)
        {
            RenderBackend& renderBackend = rendererContext.getRenderBackend();
            DistanceFieldBrickAtlas& gdfAtlasPool = scene.getLumenScene()->getGlobalDistanceFieldBrickAtlas();
            GlobalDistanceField& gdf = camera.getGlobalDistanceField();
            gdfAtlasBrickGridExtent = gdfAtlasPool.getBrickGridExtent().x;

            // M3c: per-mesh MDF 源数据——lumenUpdate.ready 已经隐含 mdfAtlasPool.isInitialized()
            // (LumenSceneUpdatePass 现在按 requireMeshDistanceFieldAtlas=true 调用, 见
            // RenderPipeline.cpp), 不需要再判断一次是否就绪
            DistanceFieldBrickAtlas& mdfAtlasPool = renderBackend.getMeshDistanceFieldBrickAtlas();
            mdfAtlasBrickGridExtent = mdfAtlasPool.getBrickGridExtent().x;
            mdfBrickAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "SparseBrickAtlas"_hs, [&]
            {
                return renderGraph.importTexture("SparseBrickAtlas"_hs, &mdfAtlasPool.getAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
            });

            meshShapeDataHandle = getOrImportResource<rg::BufferHandle>(renderGraph, "MeshShapeDataTable"_hs, [&]
            {
                return renderGraph.importBuffer("MeshShapeDataTable"_hs, renderBackend.getMeshShapeDataTable().getBuffer());
            });
            HString meshInstanceDataKey = "SceneMeshInstanceData"_hs(scene.getSceneId());
            meshInstanceDataHandle = getOrImportResource<rg::BufferHandle>(renderGraph, meshInstanceDataKey, [&]
            {
                return renderGraph.importBuffer(meshInstanceDataKey, &scene.getMeshInstanceDataBuffer());
            });
            HString gdfIndirectionKey = "GlobalDistanceFieldIndirection"_hs(camera.getViewId());
            gdfIndirectionHandle = getOrImportResource<rg::BufferHandle>(renderGraph, gdfIndirectionKey, [&]
            {
                return renderGraph.importBuffer(gdfIndirectionKey, gdf.getIndirectionBuffer());
            });
            HString gdfObjectGridKey = "GlobalDistanceFieldObjectGrid"_hs(camera.getViewId());
            gdfObjectGridHandle = getOrImportResource<rg::BufferHandle>(renderGraph, gdfObjectGridKey, [&]
            {
                return renderGraph.importBuffer(gdfObjectGridKey, gdf.getObjectGridBuffer());
            });
            HString gdfAtlasKey = "GlobalDistanceFieldAtlas"_hs(camera.getViewId());
            gdfAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, gdfAtlasKey, [&]
            {
                return renderGraph.importTexture(gdfAtlasKey, &gdfAtlasPool.getAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
            });
        }

        const uint32_t viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);
        const rhi::BindGroup* viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
        const rhi::BindGroup* instanceDataBindGroup = &scene.getInstanceDataBindGroup();

        renderGraph.addComputePass<ScreenProbeTraceData>(
            "ScreenProbeTrace"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeTraceData& data)
            {
                data.viewId = viewId;
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.atlasWidth = atlasViewSize.x;
                data.constants.numUniformProbes = tileGridSize.x * tileGridSize.y;
                data.constants.maxNumAdaptiveProbes = ScreenProbePlacementPass::getMaxNumAdaptiveProbes(tileGridSize);
                data.constants.traceFlags =
                    (screenTraceEnabled ? kScreenProbeTraceFlagScreenTraceEnabled : 0u)
                    | (depthHistoryValid ? kScreenProbeTraceFlagDepthHistoryValid : 0u)
                    | (gdfEnabled ? kScreenProbeTraceFlagGdfTraceEnabled : 0u);
                data.constants.maxWorldTraceDistance = kMaxWorldTraceDistanceMeters;
                data.constants.hzbUvFactor = hzbUvFactor;
                data.constants.hzbBaseTexelSize = hzbBaseTexelSize;
                data.constants.gdfAtlasBrickGridExtent = gdfAtlasBrickGridExtent;
                data.constants.mdfAtlasBrickGridExtent = mdfAtlasBrickGridExtent;
                data.viewBindGroup = viewBindGroup;
                data.instanceDataBindGroup = instanceDataBindGroup;
                data.viewDataDynamicOffset = viewDataDynamicOffset;
                data.useTAAHistory = useTAAHistory;
                data.gdfEnabled = gdfEnabled;

                auto& blackboard = builder.getBlackboard();
                data.probeSceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeSceneDepth"_hs(viewId)));
                data.probeWorldNormal = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeWorldNormal"_hs(viewId)));
                data.probeWorldSpeed = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeWorldSpeed"_hs(viewId)));
                data.rayInfos = builder.sample(blackboard.get<rg::TextureHandle>("ScreenProbeRayInfos"_hs(viewId)));
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.sceneHZB = builder.sample(blackboard.get<rg::TextureHandle>("SceneHZB"_hs(viewId)));
                data.velocity = builder.sample(blackboard.get<rg::TextureHandle>("Velocity"_hs(viewId)));
                if (useTAAHistory)
                    data.taaHistory = builder.sample(taaHistoryHandle);
                data.sceneDepthHistory = builder.sample(sceneDepthHistoryHandle);
                data.skyLightPrefilter = builder.sample(skyLightPrefilterHandle);
                data.skyLightData = builder.accessBuffer(skyLightDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.blueNoise = builder.sample(blueNoiseHandle);

                if (gdfEnabled)
                {
                    data.meshShapeData = builder.accessBuffer(meshShapeDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.meshInstanceData = builder.accessBuffer(meshInstanceDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.gdfIndirection = builder.accessBuffer(gdfIndirectionHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.gdfAtlas = builder.sample(gdfAtlasHandle);
                    data.mdfBrickAtlas = builder.sample(mdfBrickAtlasHandle);
                    data.objectGrid = builder.accessBuffer(gdfObjectGridHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.cardPages = builder.accessBuffer(lumenUpdate.cardPages, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.cardNormalAtlas = builder.sample(lumenUpdate.cardNormalAtlas);
                    data.cardDepthAtlas = builder.sample(lumenUpdate.cardDepthAtlas);
                    data.cardFinalLightingAtlas = builder.sample(lumenUpdate.cardFinalLightingAtlas);
                    data.cardAlbedoAtlas = builder.sample(lumenUpdate.cardAlbedoAtlas);
                    data.cardEmissiveAtlas = builder.sample(lumenUpdate.cardEmissiveAtlas);
                    data.cardDirectLightingAtlas = builder.sample(lumenUpdate.cardDirectLightingAtlas);
                    data.cardIndirectLightingAtlas = builder.sample(lumenUpdate.cardIndirectLightingAtlas);
                }

                data.adaptiveProbePosAndTileIndices = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbePosAndTileIndices"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.adaptiveProbeTotalAndTileCounts = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("AdaptiveScreenProbeTotalAndTileCounts"_hs(viewId)), rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.indirectArgs = builder.accessBuffer(
                    blackboard.get<rg::BufferHandle>("ScreenProbeIndirectArgs"_hs(viewId)), rhi::BufferUsage::Indirect, rg::AccessType::Read_Only);

                rhi::TextureDesc radianceDesc;
                radianceDesc.type = rhi::TextureType::Texture_2D;
                radianceDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                radianceDesc.extent = {
                    atlasViewSize.x * kScreenProbeTracingOctahedronResolution,
                    atlasViewSize.y * kScreenProbeTracingOctahedronResolution,
                    1u };
                radianceDesc.mipCount = 1u;
                radianceDesc.layerCount = 1u;
                radianceDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto traceRadiance = builder.createTexture("ScreenProbeTraceRadiance"_hs(viewId), radianceDesc);

                rhi::TextureDesc hitDesc = radianceDesc;
                hitDesc.format = rhi::TextureFormat::R32_UINT;
                auto traceHit = builder.createTexture("ScreenProbeTraceHit"_hs(viewId), hitDesc);

                data.traceRadiance = builder.storage(traceRadiance, rg::AccessType::Write_Only);
                data.traceHit = builder.storage(traceHit, rg::AccessType::Write_Only);

                blackboard.put("ScreenProbeTraceRadiance"_hs(viewId), data.traceRadiance);
                blackboard.put("ScreenProbeTraceHit"_hs(viewId), data.traceHit);
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeTraceData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeTracePass, ScreenProbeTraceStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.traceShader)
                {
                    storage.traceShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeTrace",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeTrace, Compute)));
                    storage.traceLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeTrace_Bindings);
                    storage.tracePipeline = createComputePipeline(rendererContext, storage.traceShader->getVariant(), *storage.traceLayout, &rendererContext.getInstanceDataBindGroupLayout());
                }

                if (storage.traceCaches.size() <= data.viewId)
                    storage.traceCaches.resize(data.viewId + 1u);
                auto& cache = storage.traceCaches[data.viewId];

                auto& defaultResources = renderBackend.getDefaultResources();
                const rhi::TextureView* probeSceneDepth = context.getTexture(data.probeSceneDepth);
                const rhi::TextureView* probeWorldNormal = context.getTexture(data.probeWorldNormal);
                const rhi::TextureView* probeWorldSpeed = context.getTexture(data.probeWorldSpeed);
                const rhi::TextureView* rayInfos = context.getTexture(data.rayInfos);
                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                const rhi::TextureView* sceneHZB = context.getTexture(data.sceneHZB);
                const rhi::TextureView* velocity = context.getTexture(data.velocity);
                const rhi::TextureView* taaHistory = data.useTAAHistory
                    ? context.getTexture(data.taaHistory)
                    : &defaultResources.getBlackTextureView();
                const rhi::TextureView* sceneDepthHistory = context.getTexture(data.sceneDepthHistory);
                const rhi::TextureView* skyLightPrefilter = context.getTexture(data.skyLightPrefilter);
                rhi::Buffer* skyLightData = context.getBuffer(data.skyLightData);
                const rhi::TextureView* blueNoise = context.getTexture(data.blueNoise);
                rhi::TextureView* traceRadiance = context.getTexture(data.traceRadiance);
                rhi::TextureView* traceHit = context.getTexture(data.traceHit);
                rhi::Buffer* adaptiveProbePosAndTileIndices = context.getBuffer(data.adaptiveProbePosAndTileIndices);
                rhi::Buffer* adaptiveProbeTotalAndTileCounts = context.getBuffer(data.adaptiveProbeTotalAndTileCounts);
                rhi::Buffer* indirectArgs = context.getBuffer(data.indirectArgs);
                rhi::Sampler* pointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();
                rhi::Sampler* linearClampSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (!storage.dummyStorageBuffer)
                {
                    rhi::BufferDesc dummyDesc;
                    dummyDesc.size = 256;
                    dummyDesc.usage = rhi::BufferUsage::Storage;
                    storage.dummyStorageBuffer = renderBackend.getDevice().createBuffer(dummyDesc, rhi::MemoryType::Gpu_Only);
                    storage.dummyStorageBuffer->setDebugName("ScreenProbeTrace.DummyStorageBuffer");
                }
                rhi::Buffer* dummyStorageBuffer = storage.dummyStorageBuffer.get();
                const rhi::TextureView* blackTexture = &defaultResources.getBlackTextureView();

                rhi::Buffer* meshShapeData = data.gdfEnabled ? context.getBuffer(data.meshShapeData) : dummyStorageBuffer;
                rhi::Buffer* meshInstanceData = data.gdfEnabled ? context.getBuffer(data.meshInstanceData) : dummyStorageBuffer;
                rhi::Buffer* gdfIndirection = data.gdfEnabled ? context.getBuffer(data.gdfIndirection) : dummyStorageBuffer;
                const rhi::TextureView* gdfAtlas = data.gdfEnabled ? context.getTexture(data.gdfAtlas) : &defaultResources.getBlackTexture3DView();
                const rhi::TextureView* mdfBrickAtlas = data.gdfEnabled ? context.getTexture(data.mdfBrickAtlas) : &defaultResources.getBlackTexture3DView();
                rhi::Buffer* objectGrid = data.gdfEnabled ? context.getBuffer(data.objectGrid) : dummyStorageBuffer;
                rhi::Buffer* cardPages = data.gdfEnabled ? context.getBuffer(data.cardPages) : dummyStorageBuffer;
                const rhi::TextureView* cardNormalAtlas = data.gdfEnabled ? context.getTexture(data.cardNormalAtlas) : blackTexture;
                const rhi::TextureView* cardDepthAtlas = data.gdfEnabled ? context.getTexture(data.cardDepthAtlas) : blackTexture;
                const rhi::TextureView* cardFinalLightingAtlas = data.gdfEnabled ? context.getTexture(data.cardFinalLightingAtlas) : blackTexture;
                const rhi::TextureView* cardAlbedoAtlas = data.gdfEnabled ? context.getTexture(data.cardAlbedoAtlas) : blackTexture;
                const rhi::TextureView* cardEmissiveAtlas = data.gdfEnabled ? context.getTexture(data.cardEmissiveAtlas) : blackTexture;
                const rhi::TextureView* cardDirectLightingAtlas = data.gdfEnabled ? context.getTexture(data.cardDirectLightingAtlas) : blackTexture;
                const rhi::TextureView* cardIndirectLightingAtlas = data.gdfEnabled ? context.getTexture(data.cardIndirectLightingAtlas) : blackTexture;

                if (cache.needUpdate(
                        probeSceneDepth, probeWorldNormal, probeWorldSpeed, rayInfos, adaptiveProbePosAndTileIndices, adaptiveProbeTotalAndTileCounts, sceneDepth,
                        sceneHZB, velocity, taaHistory, sceneDepthHistory, skyLightPrefilter, skyLightData, blueNoise,
                        pointClampSampler, linearClampSampler, traceRadiance, traceHit,
                        meshShapeData, meshInstanceData, gdfIndirection, gdfAtlas, mdfBrickAtlas, cardNormalAtlas, cardDepthAtlas,
                        objectGrid, cardPages, cardFinalLightingAtlas, cardAlbedoAtlas, cardEmissiveAtlas,
                        cardDirectLightingAtlas, cardIndirectLightingAtlas))
                {
                    cache.update(renderBackend, *storage.traceLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeTrace_Bindings);
                        writer.setTexture("ScreenProbeSceneDepth", probeSceneDepth);
                        writer.setTexture("ScreenProbeWorldNormal", probeWorldNormal);
                        writer.setTexture("ScreenProbeWorldSpeed", probeWorldSpeed);
                        writer.setTexture("ScreenProbeRayInfos", rayInfos);
                        writer.setBuffer("AdaptiveScreenProbePosAndTileIndices", adaptiveProbePosAndTileIndices);
                        writer.setBuffer("AdaptiveScreenProbeTotalAndTileCounts", adaptiveProbeTotalAndTileCounts);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("SceneHZBImage", sceneHZB);
                        writer.setTexture("VelocityImage", velocity);
                        writer.setTexture("TAAHistoryColor", taaHistory);
                        writer.setTexture("SceneDepthHistory", sceneDepthHistory);
                        writer.setTexture("SkyLightPrefilter", skyLightPrefilter);
                        writer.setBuffer("SkyLightData", skyLightData);
                        writer.setTexture("BlueNoiseVec2Texture", blueNoise);
                        writer.setSampler("PointClampSampler", pointClampSampler);
                        writer.setSampler("LinearSampler", linearClampSampler);
                        writer.setTexture("TraceRadiance", traceRadiance);
                        writer.setTexture("TraceHit", traceHit);
                        writer.setBuffer("MeshShapeData", meshShapeData);
                        writer.setBuffer("MeshInstanceDataBuffer", meshInstanceData);
                        writer.setBuffer("GdfIndirection", gdfIndirection);
                        writer.setTexture("GdfAtlas", gdfAtlas);
                        writer.setTexture("MeshDistanceFieldBrickAtlas", mdfBrickAtlas);
                        writer.setTexture("CardNormalAtlas", cardNormalAtlas);
                        writer.setTexture("CardDepthAtlas", cardDepthAtlas);
                        writer.setBuffer("ObjectGrid", objectGrid);
                        writer.setBuffer("CardPages", cardPages);
                        writer.setTexture("CardFinalLightingAtlas", cardFinalLightingAtlas);
                        writer.setTexture("CardAlbedoAtlas", cardAlbedoAtlas);
                        writer.setTexture("CardEmissiveAtlas", cardEmissiveAtlas);
                        writer.setTexture("CardDirectLightingAtlas", cardDirectLightingAtlas);
                        writer.setTexture("CardIndirectLightingAtlas", cardIndirectLightingAtlas);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.tracePipeline.get());
                cmdList.setBindGroup(1, rhi::PipelineBindPoint::Compute, &storage.tracePipeline->getLayout(), data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.tracePipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setBindGroup(3, rhi::PipelineBindPoint::Compute, &storage.tracePipeline->getLayout(), data.instanceDataBindGroup, {});
                cmdList.setImmediateData(
                    &storage.tracePipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatchIndirect(indirectArgs, 0);
            });
    }

    // 拆成独立入口是因为这一帧里可能不止 Trace pass 一个读者(ScreenProbeTemporalFilterPass 也要读
    // 上一帧的 SceneDepthHistory)——写入必须排在这一帧所有读者之后, 调用方负责选对位置, 见
    // RenderPipeline.cpp 的调用点
    void ScreenProbeTracePass::addSceneDepthHistoryCopyToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneView& view,
        PostProcessState& postProcess
    )
    {
        const uint32_t viewId = view.getViewId();
        const float4 viewport = view.getViewport();
        const uint2 viewExtent(static_cast<uint32_t>(viewport.z), static_cast<uint32_t>(viewport.w));

        rg::TextureHandle sceneDepthHistoryHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeSceneDepthHistory"_hs(viewId), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeSceneDepthHistory"_hs(viewId), &postProcess.getSceneDepthHistoryView(), rhi::ResourceState::Shader_Sampled);
        });

        renderGraph.addComputePass<ScreenProbeSceneDepthHistoryCopyData>(
            "ScreenProbeSceneDepthHistoryCopy"_hs,
            [&](rg::RenderGraph::Builder& builder, ScreenProbeSceneDepthHistoryCopyData& data)
            {
                data.viewId = viewId;
                data.extent = viewExtent;
                data.sceneDepth = builder.sample(builder.getBlackboard().get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.sceneDepthHistory = builder.storage(sceneDepthHistoryHandle, rg::AccessType::Write_Only);
                builder.markSideEffect();
            },
            [&rendererContext](rg::ExecuteContext& context, ScreenProbeSceneDepthHistoryCopyData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ScreenProbeTracePass, ScreenProbeTraceStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.copyShader)
                {
                    storage.copyShader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ScreenProbeSceneDepthHistoryCopy",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ScreenProbeSceneDepthHistoryCopy, Compute)));
                    storage.copyLayout = createPassBindGroupLayout(renderBackend, generated::kScreenProbeSceneDepthHistoryCopy_Bindings);
                    storage.copyPipeline = createComputePipeline(rendererContext, storage.copyShader->getVariant(), *storage.copyLayout);
                }

                if (storage.copyCaches.size() <= data.viewId)
                    storage.copyCaches.resize(data.viewId + 1u);
                auto& cache = storage.copyCaches[data.viewId];

                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                rhi::TextureView* sceneDepthHistory = context.getTexture(data.sceneDepthHistory);

                if (cache.needUpdate(sceneDepth, sceneDepthHistory))
                {
                    cache.update(renderBackend, *storage.copyLayout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kScreenProbeSceneDepthHistoryCopy_Bindings);
                        writer.setTexture("SceneDepth", sceneDepth);
                        writer.setTexture("SceneDepthHistory", sceneDepthHistory);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.copyPipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.copyPipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.dispatch((data.extent.x + 7u) / 8u, (data.extent.y + 7u) / 8u, 1u);
            });

        postProcess.markSceneDepthHistoryWritten();
    }
}
