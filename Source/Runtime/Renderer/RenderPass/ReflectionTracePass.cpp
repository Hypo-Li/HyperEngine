#include "ReflectionTracePass.h"
#include "RenderPassUtils.h"
#include "HZBPass.h"
#include "../SkyLightProxy.h"
#include "../CameraProxy.h"
#include "../GlobalDistanceField.h"
#include "../LumenScene.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>

#include <ReflectionTrace.generated.h>

#include <algorithm>

namespace hyp
{
    namespace
    {
        constexpr uint32_t kThreadGroupSize = 8u;

        // UE Lumen::GetMaxTraceDistance() 出厂值 20000cm, 本引擎单位是米, 跟 ScreenProbeTracePass.cpp
        // 的 kMaxWorldTraceDistanceMeters 保持一致
        constexpr float kMaxWorldTraceDistanceMeters = 200.0f;

        uint2 dispatchGroupCountFor8(uint2 extent)
        {
            return uint2(
                std::max((extent.x + kThreadGroupSize - 1u) / kThreadGroupSize, 1u),
                std::max((extent.y + kThreadGroupSize - 1u) / kThreadGroupSize, 1u));
        }

        struct ReflectionTracePushConstants
        {
            uint2 viewRectMin;
            uint2 viewExtent;
            float2 hzbUvFactor;
            float2 hzbBaseTexelSize;
            uint32_t traceFlags;
            float maxWorldTraceDistance;
            int32_t gdfAtlasBrickGridExtent;
            int32_t mdfAtlasBrickGridExtent;
        };
        static_assert(sizeof(ReflectionTracePushConstants) == 48);

        constexpr uint32_t kReflectionTraceFlagScreenTraceEnabled = 1u << 0;
        constexpr uint32_t kReflectionTraceFlagDepthHistoryValid = 1u << 1;
        constexpr uint32_t kReflectionTraceFlagGdfTraceEnabled = 1u << 2;
    }

    struct ReflectionTraceData
    {
        uint32_t viewId;
        uint2 dispatchGroupCount;
        ReflectionTracePushConstants constants;
        const rhi::BindGroup* instanceDataBindGroup;
        bool useTAAHistory;
        bool gdfEnabled;
        rg::TextureHandle rayBuffer;
        rg::TextureHandle rayDepth;
        rg::TextureHandle gbufferA;
        rg::TextureHandle sceneDepth;
        rg::TextureHandle sceneHZB;
        rg::TextureHandle velocity;
        rg::TextureHandle taaHistory;
        rg::TextureHandle sceneDepthHistory;
        rg::TextureHandle skyLightPrefilter;
        rg::BufferHandle skyLightData;
        rg::TextureHandle traceRadiance;
        rg::TextureHandle traceHit;
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
    };

    struct ReflectionTraceStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> layout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<28>> caches;
        // GDF 追踪没启用时的占位, 只为让 bind group 完整
        TRef<rhi::Buffer> dummyStorageBuffer;
    };

    void ReflectionTracePass::addToRenderGraph(
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

        const uint2 hzbBaseExtent = HZBPass::getBaseExtent(viewExtent);
        const float2 hzbUvFactor(
            float(viewExtent.x) / float(2u * hzbBaseExtent.x),
            float(viewExtent.y) / float(2u * hzbBaseExtent.y));
        const float2 hzbBaseTexelSize(1.0f / float(hzbBaseExtent.x), 1.0f / float(hzbBaseExtent.y));

        // ReflectionGenerateRaysPass/ScreenProbeTracePass 这一帧都要用同一份历史资源, 键名必须
        // 跟 ScreenProbeTracePass.cpp 完全一致才能命中 getOrImportResource 的复用路径
        postProcess.ensureSceneDepthHistory(viewExtent);
        bool useTAAHistory = false;
        if (postProcess.isJitterEnabled())
        {
            postProcess.ensureTAAHistory(viewExtent);
            useTAAHistory = postProcess.isTAAHistoryValid();
        }
        const bool depthHistoryValid = postProcess.isSceneDepthHistoryValid();
        const bool screenTraceEnabled = useTAAHistory && !scene.getDisableScreenTrace();

        rg::TextureHandle sceneDepthHistoryHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeSceneDepthHistory"_hs(viewId), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeSceneDepthHistory"_hs(viewId), &postProcess.getSceneDepthHistoryView(), rhi::ResourceState::Shader_Sampled);
        });
        rg::TextureHandle taaHistoryHandle;
        if (useTAAHistory)
        {
            taaHistoryHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeTraceTAAHistory"_hs(viewId), [&]
            {
                return renderGraph.importTexture(
                    "ScreenProbeTraceTAAHistory"_hs(viewId), &postProcess.getTAAHistoryReadView(), rhi::ResourceState::Shader_Sampled);
            });
        }

        SkyLightProxy* activeSkyLight = scene.getActiveSkyLightProxy();
        TextureProxy* skyLightPrefilterProxy = activeSkyLight ? activeSkyLight->getSpecularPrefilter() : nullptr;
        rhi::TextureView& skyLightPrefilterView = skyLightPrefilterProxy
            ? skyLightPrefilterProxy->getTextureView()
            : rendererContext.getRenderBackend().getDefaultResources().getBlackTextureCubeView();
        rg::TextureHandle skyLightPrefilterHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "ScreenProbeTraceActiveSkyLightPrefilter"_hs(scene.getSceneId()), [&]
        {
            return renderGraph.importTexture(
                "ScreenProbeTraceActiveSkyLightPrefilter"_hs(scene.getSceneId()), &skyLightPrefilterView, rhi::ResourceState::Shader_Sampled);
        });
        rhi::Buffer& skyLightDataBuffer = activeSkyLight
            ? activeSkyLight->getDataBuffer()
            : rendererContext.getRenderBackend().getDefaultResources().getDummyStorageBuffer();
        rg::BufferHandle skyLightDataHandle = getOrImportResource<rg::BufferHandle>(renderGraph, "ScreenProbeTraceActiveSkyLightData"_hs(scene.getSceneId()), [&]
        {
            return renderGraph.importBuffer("ScreenProbeTraceActiveSkyLightData"_hs(scene.getSceneId()), &skyLightDataBuffer);
        });

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

        renderGraph.addComputePass<ReflectionTraceData>(
            "ReflectionTrace"_hs,
            [&](rg::RenderGraph::Builder& builder, ReflectionTraceData& data)
            {
                data.viewId = viewId;
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.dispatchGroupCount = dispatchGroupCountFor8(viewExtent);
                data.constants.viewRectMin = viewRectMin;
                data.constants.viewExtent = viewExtent;
                data.constants.hzbUvFactor = hzbUvFactor;
                data.constants.hzbBaseTexelSize = hzbBaseTexelSize;
                data.constants.traceFlags =
                    (screenTraceEnabled ? kReflectionTraceFlagScreenTraceEnabled : 0u)
                    | (depthHistoryValid ? kReflectionTraceFlagDepthHistoryValid : 0u)
                    | (gdfEnabled ? kReflectionTraceFlagGdfTraceEnabled : 0u);
                data.constants.maxWorldTraceDistance = kMaxWorldTraceDistanceMeters;
                data.constants.gdfAtlasBrickGridExtent = gdfAtlasBrickGridExtent;
                data.constants.mdfAtlasBrickGridExtent = mdfAtlasBrickGridExtent;
                data.useTAAHistory = useTAAHistory;
                data.gdfEnabled = gdfEnabled;

                auto& blackboard = builder.getBlackboard();
                data.rayBuffer = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionRayBuffer"_hs(viewId)));
                data.rayDepth = builder.sample(blackboard.get<rg::TextureHandle>("ReflectionRayDepth"_hs(viewId)));
                data.gbufferA = builder.sample(blackboard.get<rg::TextureHandle>("GBufferA"_hs(viewId)));
                data.sceneDepth = builder.sample(blackboard.get<rg::TextureHandle>("SceneDepthD"_hs(viewId)));
                data.sceneHZB = builder.sample(blackboard.get<rg::TextureHandle>("SceneHZB"_hs(viewId)));
                data.velocity = builder.sample(blackboard.get<rg::TextureHandle>("Velocity"_hs(viewId)));
                if (useTAAHistory)
                    data.taaHistory = builder.sample(taaHistoryHandle);
                data.sceneDepthHistory = builder.sample(sceneDepthHistoryHandle);
                data.skyLightPrefilter = builder.sample(skyLightPrefilterHandle);
                data.skyLightData = builder.accessBuffer(skyLightDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

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

                rhi::TextureDesc radianceDesc;
                radianceDesc.type = rhi::TextureType::Texture_2D;
                radianceDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                radianceDesc.extent = { viewExtent.x, viewExtent.y, 1u };
                radianceDesc.mipCount = 1u;
                radianceDesc.layerCount = 1u;
                radianceDesc.usage = rhi::TextureUsage::Storage | rhi::TextureUsage::Sampled;
                auto traceRadiance = builder.createTexture("ReflectionTraceRadiance"_hs(viewId), radianceDesc);

                rhi::TextureDesc hitDesc = radianceDesc;
                hitDesc.format = rhi::TextureFormat::R16_SFLOAT;
                auto traceHit = builder.createTexture("ReflectionTraceHit"_hs(viewId), hitDesc);

                data.traceRadiance = builder.storage(traceRadiance, rg::AccessType::Write_Only);
                data.traceHit = builder.storage(traceHit, rg::AccessType::Write_Only);
                blackboard.put("ReflectionTraceRadiance"_hs(viewId), data.traceRadiance);
                blackboard.put("ReflectionTraceHit"_hs(viewId), data.traceHit);
            },
            [&rendererContext](rg::ExecuteContext& context, ReflectionTraceData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<ReflectionTracePass, ReflectionTraceStorage>();
                auto& renderBackend = context.getRenderBackend();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "ReflectionTrace",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(ReflectionTrace, Compute)));
                    storage.layout = createPassBindGroupLayout(renderBackend, generated::kReflectionTrace_Bindings);
                    storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.layout, &rendererContext.getInstanceDataBindGroupLayout());
                }

                if (storage.caches.size() <= data.viewId)
                    storage.caches.resize(data.viewId + 1u);
                auto& cache = storage.caches[data.viewId];

                auto& defaultResources = renderBackend.getDefaultResources();
                const rhi::TextureView* rayBuffer = context.getTexture(data.rayBuffer);
                const rhi::TextureView* rayDepth = context.getTexture(data.rayDepth);
                const rhi::TextureView* gbufferA = context.getTexture(data.gbufferA);
                const rhi::TextureView* sceneDepth = context.getTexture(data.sceneDepth);
                const rhi::TextureView* sceneHZB = context.getTexture(data.sceneHZB);
                const rhi::TextureView* velocity = context.getTexture(data.velocity);
                const rhi::TextureView* taaHistory = data.useTAAHistory
                    ? context.getTexture(data.taaHistory)
                    : &defaultResources.getBlackTextureView();
                const rhi::TextureView* sceneDepthHistory = context.getTexture(data.sceneDepthHistory);
                const rhi::TextureView* skyLightPrefilter = context.getTexture(data.skyLightPrefilter);
                rhi::Buffer* skyLightData = context.getBuffer(data.skyLightData);
                rhi::TextureView* traceRadiance = context.getTexture(data.traceRadiance);
                rhi::TextureView* traceHit = context.getTexture(data.traceHit);
                rhi::Sampler* pointClampSampler = renderBackend.getSamplerCache().getPointClampNoMip();
                rhi::Sampler* linearClampSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (!storage.dummyStorageBuffer)
                {
                    rhi::BufferDesc dummyDesc;
                    dummyDesc.size = 256;
                    dummyDesc.usage = rhi::BufferUsage::Storage;
                    storage.dummyStorageBuffer = renderBackend.getDevice().createBuffer(dummyDesc, rhi::MemoryType::Gpu_Only);
                    storage.dummyStorageBuffer->setDebugName("ReflectionTrace.DummyStorageBuffer");
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
                        rayBuffer, rayDepth, gbufferA, sceneDepth, sceneHZB, velocity, taaHistory, sceneDepthHistory,
                        skyLightPrefilter, skyLightData, pointClampSampler, linearClampSampler, traceRadiance, traceHit,
                        meshShapeData, meshInstanceData, gdfIndirection, gdfAtlas, mdfBrickAtlas, cardNormalAtlas, cardDepthAtlas,
                        objectGrid, cardPages, cardFinalLightingAtlas, cardAlbedoAtlas, cardEmissiveAtlas,
                        cardDirectLightingAtlas, cardIndirectLightingAtlas))
                {
                    cache.update(renderBackend, *storage.layout, [&](rhi::BindGroup& bindGroup)
                    {
                        PassBindGroupWriter writer(generated::kReflectionTrace_Bindings);
                        writer.setTexture("RayBuffer", rayBuffer);
                        writer.setTexture("RayDepth", rayDepth);
                        writer.setTexture("GbufferAImage", gbufferA);
                        writer.setTexture("SceneDepthImage", sceneDepth);
                        writer.setTexture("SceneHZBImage", sceneHZB);
                        writer.setTexture("VelocityImage", velocity);
                        writer.setTexture("TAAHistoryColor", taaHistory);
                        writer.setTexture("SceneDepthHistory", sceneDepthHistory);
                        writer.setTexture("SkyLightPrefilter", skyLightPrefilter);
                        writer.setBuffer("SkyLightData", skyLightData);
                        writer.setSampler("PointClampSampler", pointClampSampler);
                        writer.setSampler("LinearSampler", linearClampSampler);
                        writer.setTexture("TraceRadiance", traceRadiance);
                        writer.setTexture("TraceHit", traceHit);
                        writer.setBuffer("MeshShapeData", meshShapeData);
                        writer.setBuffer("MeshInstanceDataBuffer", meshInstanceData);
                        writer.setBuffer("GdfIndirection", gdfIndirection);
                        writer.setTexture("GdfAtlas", gdfAtlas);
                        writer.setTexture("CardNormalAtlas", cardNormalAtlas);
                        writer.setTexture("CardDepthAtlas", cardDepthAtlas);
                        writer.setBuffer("ObjectGrid", objectGrid);
                        writer.setBuffer("CardPages", cardPages);
                        writer.setTexture("CardFinalLightingAtlas", cardFinalLightingAtlas);
                        writer.setTexture("CardAlbedoAtlas", cardAlbedoAtlas);
                        writer.setTexture("CardEmissiveAtlas", cardEmissiveAtlas);
                        writer.setTexture("CardDirectLightingAtlas", cardDirectLightingAtlas);
                        writer.setTexture("CardIndirectLightingAtlas", cardIndirectLightingAtlas);
                        writer.setTexture("MeshDistanceFieldBrickAtlas", mdfBrickAtlas);
                        writer.apply(bindGroup);
                    });
                }

                auto& cmdList = context.getCommandList();
                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(2, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(), cache.getBindGroup(), {});
                cmdList.setBindGroup(3, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(), data.instanceDataBindGroup, {});
                cmdList.setImmediateData(
                    &storage.pipeline->getLayout(), 0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants)));
                cmdList.dispatch(data.dispatchGroupCount.x, data.dispatchGroupCount.y, 1u);
            });
    }
}
