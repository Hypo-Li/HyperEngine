#include "RadiosityPass.h"
#include "RenderPassUtils.h"
#include "../SurfaceCacheAtlas.h"
#include "../LumenScene.h"
#include "../CameraProxy.h"
#include "../GlobalDistanceField.h"
#include "../SkyLightProxy.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/MeshShapeProxy.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>
#include <RenderBase/DefaultResources.h>

#include <RadiosityTrace.generated.h>
#include <RadiosityFilter.generated.h>
#include <RadiosityConvertToSH.generated.h>
#include <RadiosityIntegrate.generated.h>

#include <algorithm>
#include <bit>
#include <cstring>
#include <span>
#include <vector>

namespace hyp
{
    namespace
    {
        // ============================== [A] Trace ==============================

        struct RadiosityTraceConstants
        {
            float cardCenterX, cardCenterY, cardCenterZ;
            float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
            uint32_t directionIndex;
            uint32_t packedAddress; // mCardPageTable 打包值, shader 端用 decodeCardPageAddress() 解出像素原点+实际尺寸
            uint32_t pageIndexX, pageIndexY;
            uint32_t pageCountX, pageCountY;
            uint32_t instanceIndex;
            int32_t gdfAtlasBrickGridExtent;
            uint32_t temporalIndex;
        };
        static_assert(sizeof(RadiosityTraceConstants) == 60);

        struct RadiosityTracePageTask
        {
            RadiosityTraceConstants constants;
            uint32_t groupsX;
            uint32_t groupsY;
        };

        struct RadiosityTraceData
        {
            std::vector<RadiosityTracePageTask> tasks;
            const rhi::BindGroup* viewBindGroup;
            const rhi::BindGroup* instanceDataBindGroup;
            uint32_t viewDataDynamicOffset;
            rg::BufferHandle meshShapeData;
            rg::BufferHandle meshInstanceData;
            rg::BufferHandle gdfIndirection;
            rg::TextureHandle gdfAtlas;
            rg::TextureHandle cardNormalAtlas;
            rg::TextureHandle cardDepthAtlas;
            rg::BufferHandle objectGrid;
            rg::BufferHandle cardPages;
            rg::TextureHandle cardFinalLightingAtlas;
            rg::TextureHandle cardAlbedoAtlas;
            rg::TextureHandle skyLightPrefilter;
            rg::BufferHandle skyLightData;
            rg::TextureHandle traceRadianceAtlas;
            // resolveGlobalDistanceFieldHit() 的必传参数, 这条路径只用它混合出的
            // cardLighting(FinalLighting), 不关心这三张各自的采样结果
            rg::TextureHandle cardEmissiveAtlas;
            rg::TextureHandle cardDirectLightingAtlas;
            rg::TextureHandle cardIndirectLightingAtlas;
            rg::TextureHandle blueNoiseVec2Texture;
        };

        struct RadiosityTraceStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            TRef<rhi::Pipeline> pipeline;
            BindGroupCache<19> bindGroupCache;

            TRef<rhi::Buffer> sceneConstantsBuffer;
            bool sceneConstantsBufferInitialized = false;
        };

        // ============================== [B] SpatialFilter ==============================

        // 跟 RadiosityIntegrateConstants 同一套页身份字段(含 pageIndirectionOffset, 跨页寻址
        // 要用), 少了 IndirectLighting 相关的部分
        struct RadiosityFilterConstants
        {
            float cardCenterX, cardCenterY, cardCenterZ;
            float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
            uint32_t directionIndex;
            uint32_t packedAddress; // mCardPageTable 打包值, shader 端用 decodeCardPageAddress() 解出像素原点+实际尺寸
            uint32_t pageIndexX, pageIndexY;
            uint32_t pageCountX, pageCountY;
            uint32_t pageIndirectionOffset;
            uint32_t instanceIndex;
            uint32_t temporalIndex;
        };
        static_assert(sizeof(RadiosityFilterConstants) == 60);

        struct RadiosityFilterPageTask
        {
            RadiosityFilterConstants constants;
            uint32_t groupsX;
            uint32_t groupsY;
        };

        struct RadiosityFilterData
        {
            std::vector<RadiosityFilterPageTask> tasks;
            const rhi::BindGroup* viewBindGroup;
            const rhi::BindGroup* instanceDataBindGroup;
            uint32_t viewDataDynamicOffset;
            rg::TextureHandle cardNormalAtlas;
            rg::TextureHandle cardDepthAtlas;
            rg::TextureHandle traceRadianceAtlas;
            rg::BufferHandle cardPages;
            rg::TextureHandle filteredTraceRadianceAtlas;
        };

        struct RadiosityFilterStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            TRef<rhi::Pipeline> pipeline;
            BindGroupCache<5> bindGroupCache;
        };

        // ============================== [C] ConvertToSH ==============================

        struct RadiosityConvertToSHConstants
        {
            float cardCenterX, cardCenterY, cardCenterZ;
            float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
            uint32_t directionIndex;
            uint32_t packedAddress; // mCardPageTable 打包值, shader 端用 decodeCardPageAddress() 解出像素原点+实际尺寸
            uint32_t pageIndexX, pageIndexY;
            uint32_t pageCountX, pageCountY;
            uint32_t instanceIndex;
            uint32_t temporalIndex;
        };
        static_assert(sizeof(RadiosityConvertToSHConstants) == 56);

        struct RadiosityConvertToSHPageTask
        {
            RadiosityConvertToSHConstants constants;
            uint32_t groupsX;
            uint32_t groupsY;
        };

        struct RadiosityConvertToSHData
        {
            std::vector<RadiosityConvertToSHPageTask> tasks;
            const rhi::BindGroup* viewBindGroup;
            const rhi::BindGroup* instanceDataBindGroup;
            uint32_t viewDataDynamicOffset;
            rg::TextureHandle cardNormalAtlas;
            rg::TextureHandle cardDepthAtlas;
            rg::TextureHandle filteredTraceRadianceAtlas;
            rg::TextureHandle probeSHRAtlas;
            rg::TextureHandle probeSHGAtlas;
            rg::TextureHandle probeSHBAtlas;
            rg::TextureHandle blueNoiseVec2Texture;
        };

        struct RadiosityConvertToSHStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            TRef<rhi::Pipeline> pipeline;
            BindGroupCache<7> bindGroupCache;
        };

        // ============================== [D] Integrate ==============================

        struct RadiosityIntegrateConstants
        {
            float cardCenterX, cardCenterY, cardCenterZ;
            float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
            uint32_t directionIndex;
            uint32_t packedAddress; // mCardPageTable 打包值, shader 端用 decodeCardPageAddress() 解出像素原点+实际尺寸
            uint32_t pageIndexX, pageIndexY;
            uint32_t pageCountX, pageCountY;
            uint32_t pageIndirectionOffset;
            uint32_t instanceIndex;
            uint32_t temporalIndex;
        };
        static_assert(sizeof(RadiosityIntegrateConstants) == 60);

        struct RadiosityIntegratePageTask
        {
            RadiosityIntegrateConstants constants;
            uint32_t groupsX;
            uint32_t groupsY;
        };

        struct RadiosityIntegrateData
        {
            std::vector<RadiosityIntegratePageTask> tasks;
            const rhi::BindGroup* viewBindGroup;
            const rhi::BindGroup* instanceDataBindGroup;
            uint32_t viewDataDynamicOffset;
            rg::TextureHandle cardNormalAtlas;
            rg::TextureHandle cardDepthAtlas;
            rg::TextureHandle probeSHRAtlas;
            rg::TextureHandle probeSHGAtlas;
            rg::TextureHandle probeSHBAtlas;
            rg::BufferHandle cardPages;
            rg::TextureHandle indirectLightingAtlas;
            rg::TextureHandle numFramesAccumulatedAtlas;
        };

        struct RadiosityIntegrateStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            TRef<rhi::Pipeline> pipeline;
            BindGroupCache<8> bindGroupCache;
        };

        rhi::Pipeline* createRadiosityComputePipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout)
        {
            return createComputePipeline(rendererContext, shaderVariant, bindGroupLayout, &rendererContext.getInstanceDataBindGroupLayout());
        }
    }

    void RadiosityPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const CameraProxy& cameraProxy
    )
    {
        addTracePass(renderGraph, rendererContext, scene, cameraProxy);
        addFilterPass(renderGraph, rendererContext, scene, cameraProxy);
        addConvertToSHPass(renderGraph, rendererContext, scene, cameraProxy);
        addIntegratePass(renderGraph, rendererContext, scene, cameraProxy);
    }

    void RadiosityPass::addTracePass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const CameraProxy& cameraProxy
    )
    {
        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
            return;
        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();

        RenderBackend& renderBackend = rendererContext.getRenderBackend();
        MeshShapeDataTable& objectTable = renderBackend.getMeshShapeDataTable();
        DistanceFieldBrickAtlas& gdfAtlasPool = lumenScene->getGlobalDistanceFieldBrickAtlas();
        GlobalDistanceField& gdf = cameraProxy.getGlobalDistanceField();

        // "CardAtlasDirectLighting"_hs 这一帧只有 CardDirectLightingPass 真正跑过才会被写入,
        // 调用方保证这个 pass 排在它之后——理由见 CombineFinalLightingPass.cpp 同样的检查
        auto directLightingAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("CardAtlasDirectLighting"_hs);
        rhi::Buffer* gdfIndirectionBuffer = gdf.getIndirectionBuffer();
        rhi::Buffer* gdfObjectGridBuffer = gdf.getObjectGridBuffer();
        if (!directLightingAtlasHandle.isValid()
            || objectTable.hasPendingUploads()
            || !gdfAtlasPool.isInitialized()
            || !gdfIndirectionBuffer
            || !gdfObjectGridBuffer)
        {
            return;
        }

        const int3 gdfBrickGridExtent = gdfAtlasPool.getBrickGridExtent();

        std::vector<RadiosityTracePageTask> tasks;
        for (const SurfaceCacheLightingPage& page : lumenScene->getLightingSchedule().indirect)
        {
            const MeshCard& card = page.proxy->getMeshShapeProxy()->getCards()[page.cardIndex];

            RadiosityTracePageTask task;
            task.constants = RadiosityTraceConstants{};
            task.constants.cardCenterX = card.center.x;
            task.constants.cardCenterY = card.center.y;
            task.constants.cardCenterZ = card.center.z;
            task.constants.cardHalfExtentsX = card.halfExtents.x;
            task.constants.cardHalfExtentsY = card.halfExtents.y;
            task.constants.cardHalfExtentsZ = card.halfExtents.z;
            task.constants.directionIndex = card.directionIndex;
            task.constants.packedAddress = page.packedAddress;
            task.constants.pageIndexX = page.pageIndexX;
            task.constants.pageIndexY = page.pageIndexY;
            task.constants.pageCountX = page.pageCountX;
            task.constants.pageCountY = page.pageCountY;
            task.constants.instanceIndex = page.proxy->getInstanceIndex();
            task.constants.temporalIndex = page.indirectTemporalIndex;
            task.constants.gdfAtlasBrickGridExtent = gdfBrickGridExtent.x;
            task.groupsX = (uint32_t(page.extent.x) + 7) / 8;
            task.groupsY = (uint32_t(page.extent.y) + 7) / 8;
            tasks.push_back(task);
        }

        if (tasks.empty())
            return;

        const WorldCoord& referenceOrigin = lumenScene->getDirectLightingReferenceOrigin();

        auto meshShapeDataHandle = getOrImportResource<rg::BufferHandle>(renderGraph, "MeshShapeDataTable"_hs, [&]
        {
            return renderGraph.importBuffer("MeshShapeDataTable"_hs, objectTable.getBuffer());
        });
        HString meshInstanceDataKey = "SceneMeshInstanceData"_hs(scene.getSceneId());
        auto meshInstanceDataHandle = getOrImportResource<rg::BufferHandle>(renderGraph, meshInstanceDataKey, [&]
        {
            return renderGraph.importBuffer(meshInstanceDataKey, &scene.getMeshInstanceDataBuffer());
        });
        HString gdfIndirectionKey = "GlobalDistanceFieldIndirection"_hs(cameraProxy.getViewId());
        auto gdfIndirectionHandle = getOrImportResource<rg::BufferHandle>(renderGraph, gdfIndirectionKey, [&]
        {
            return renderGraph.importBuffer(gdfIndirectionKey, gdfIndirectionBuffer);
        });
        HString gdfAtlasKey = "GlobalDistanceFieldAtlas"_hs(cameraProxy.getViewId());
        auto gdfAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, gdfAtlasKey, [&]
        {
            return renderGraph.importTexture(gdfAtlasKey, &gdfAtlasPool.getAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        });
        HString gdfObjectGridKey = "GlobalDistanceFieldObjectGrid"_hs(cameraProxy.getViewId());
        auto objectGridHandle = getOrImportResource<rg::BufferHandle>(renderGraph, gdfObjectGridKey, [&]
        {
            return renderGraph.importBuffer(gdfObjectGridKey, gdfObjectGridBuffer);
        });
        auto cardNormalAtlasHandle = renderGraph.importTexture(
            "CardAtlasNormal"_hs, &surfaceCacheAtlas.getNormalAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto cardDepthAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasDepth"_hs, [&]
        {
            return renderGraph.importTexture("CardAtlasDepth"_hs, &surfaceCacheAtlas.getDepthAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        });
        auto cardPagesHandle = getOrImportResource<rg::BufferHandle>(renderGraph, "MeshSDFCardPages"_hs, [&]
        {
            return renderGraph.importBuffer("MeshSDFCardPages"_hs, surfaceCacheAtlas.getCardPageTableBuffer());
        });
        auto cardAlbedoAtlasHandle = renderGraph.importTexture(
            "CardAtlasAlbedo"_hs, &surfaceCacheAtlas.getAlbedoAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        // resolveGlobalDistanceFieldHit() 的必传参数, 这条路径不关心它的采样结果(只用
        // cardLighting), 理由见 RadiosityTraceData::cardEmissiveAtlas 的注释
        auto cardEmissiveAtlasHandle = renderGraph.importTexture(
            "CardAtlasEmissive"_hs, &surfaceCacheAtlas.getEmissiveAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        // 全程只读的引擎常驻资源, 跟 CardAtlasAlbedo 同一个理由独立 import, 不用 getOrImportResource
        auto blueNoiseVec2Handle = renderGraph.importTexture(
            "BlueNoiseVec2Texture"_hs, &renderBackend.getDefaultResources().getBlueNoiseVec2View(), rhi::ResourceState::Shader_Sampled);

        // 天光镜面预滤波 mip 0 当方向性天空辐射度用, 见 Document/Lumen_Radiosity_Plan.md §6.1。
        // 没有活跃天光就绑黑色 cube, 等价于天空辐射度恒为 0, 不需要在 shader 端另开分支
        SkyLightProxy* activeSkyLight = scene.getActiveSkyLightProxy();
        TextureProxy* skyLightPrefilterProxy = activeSkyLight ? activeSkyLight->getSpecularPrefilter() : nullptr;
        rhi::TextureView& skyLightPrefilterView = skyLightPrefilterProxy
            ? skyLightPrefilterProxy->getTextureView()
            : renderBackend.getDefaultResources().getBlackTextureCubeView();
        auto skyLightPrefilterHandle = renderGraph.importTexture(
            "RadiosityActiveSkyLightPrefilter"_hs(scene.getSceneId()), &skyLightPrefilterView, rhi::ResourceState::Shader_Sampled);
        rhi::Buffer& skyLightDataBuffer = activeSkyLight
            ? activeSkyLight->getDataBuffer()
            : renderBackend.getDefaultResources().getDummyStorageBuffer();
        auto skyLightDataHandle = renderGraph.importBuffer(
            "RadiosityActiveSkyLightData"_hs(scene.getSceneId()), &skyLightDataBuffer);

        renderGraph.addComputePass<RadiosityTraceData>(
            "RadiosityTrace"_hs,
            [&](rg::RenderGraph::Builder& builder, RadiosityTraceData& data)
            {
                builder.markSideEffect();

                data.tasks = std::move(tasks);
                data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(cameraProxy.getViewId());
                data.meshShapeData = builder.accessBuffer(meshShapeDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.meshInstanceData = builder.accessBuffer(meshInstanceDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.gdfIndirection = builder.accessBuffer(gdfIndirectionHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.gdfAtlas = builder.sample(gdfAtlasHandle);
                data.cardNormalAtlas = builder.sample(cardNormalAtlasHandle);
                data.cardDepthAtlas = builder.sample(cardDepthAtlasHandle);
                data.objectGrid = builder.accessBuffer(objectGridHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.cardPages = builder.accessBuffer(cardPagesHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.cardFinalLightingAtlas = builder.sample(getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasFinalLighting"_hs, [&]
                {
                    return renderGraph.importTexture("CardAtlasFinalLighting"_hs, &surfaceCacheAtlas.getFinalLightingAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
                }));
                data.cardAlbedoAtlas = builder.sample(cardAlbedoAtlasHandle);
                data.cardEmissiveAtlas = builder.sample(cardEmissiveAtlasHandle);
                // 这一帧只有 CardDirectLightingPass 真正跑过才会有效——早退检查已经在函数
                // 顶部确认过 directLightingAtlasHandle.isValid()
                data.cardDirectLightingAtlas = builder.sample(directLightingAtlasHandle);
                // IndirectLightingAtlas 是持久资源(见 SurfaceCacheAtlas.h 的注释), 这里读到的
                // 是 [D] Integrate 上一帧写完的版本——跟 cardFinalLightingAtlas 同一个理由用
                // getOrImportResource, 这条路径同样不关心它的采样结果
                data.cardIndirectLightingAtlas = builder.sample(getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasIndirectLighting"_hs, [&]
                {
                    return renderGraph.importTexture("CardAtlasIndirectLighting"_hs, &surfaceCacheAtlas.getIndirectLightingAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
                }));
                data.skyLightPrefilter = builder.sample(skyLightPrefilterHandle);
                data.skyLightData = builder.accessBuffer(skyLightDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.blueNoiseVec2Texture = builder.sample(blueNoiseVec2Handle);

                rhi::TextureDesc traceRadianceDesc;
                traceRadianceDesc.type = rhi::TextureType::Texture_2D;
                traceRadianceDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                traceRadianceDesc.extent = uint3(
                    uint32_t(kSurfaceCacheAtlasResolution),
                    uint32_t(kSurfaceCacheAtlasResolution),
                    1);
                traceRadianceDesc.mipCount = 1;
                traceRadianceDesc.layerCount = 1;
                traceRadianceDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
                auto traceRadianceAtlas = builder.createTexture("RadiosityTraceRadianceAtlas"_hs, traceRadianceDesc);
                data.traceRadianceAtlas = builder.storage(traceRadianceAtlas, rg::AccessType::Write_Only);

                renderGraph.getBlackboard().put("RadiosityTraceRadianceAtlas"_hs, data.traceRadianceAtlas);
            },
            [&rendererContext, referenceOrigin](rg::ExecuteContext& context, RadiosityTraceData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<RadiosityPass, RadiosityTraceStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.sceneConstantsBuffer)
                {
                    rhi::BufferDesc bufferDesc;
                    bufferDesc.size = 6 * sizeof(uint32_t);
                    bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
                    storage.sceneConstantsBuffer = renderBackend.getDevice().createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
                    storage.sceneConstantsBuffer->setDebugName("RadiosityPass.Trace.SceneConstants");
                }

                {
                    uint32_t sceneConstants[6];
                    sceneConstants[0] = static_cast<uint32_t>(referenceOrigin.tile.x);
                    sceneConstants[1] = static_cast<uint32_t>(referenceOrigin.tile.y);
                    sceneConstants[2] = static_cast<uint32_t>(referenceOrigin.tile.z);
                    sceneConstants[3] = std::bit_cast<uint32_t>(referenceOrigin.offset.x);
                    sceneConstants[4] = std::bit_cast<uint32_t>(referenceOrigin.offset.y);
                    sceneConstants[5] = std::bit_cast<uint32_t>(referenceOrigin.offset.z);

                    auto& stagingAllocator = renderBackend.getStagingAllocator();
                    auto stagingAllocation = stagingAllocator.tryAllocate(sizeof(sceneConstants));
                    if (stagingAllocation)
                    {
                        std::memcpy(stagingAllocation->mappedData, sceneConstants, sizeof(sceneConstants));

                        rhi::BufferBarrier preBarrier;
                        preBarrier.buffer = storage.sceneConstantsBuffer.get();
                        preBarrier.offset = 0;
                        preBarrier.size = rhi::kWholeBuffer;
                        preBarrier.oldState = storage.sceneConstantsBufferInitialized ? rhi::ResourceState::Shader_Storage_Read : rhi::ResourceState::None;
                        preBarrier.newState = rhi::ResourceState::Transfer_Dst;
                        cmdList.insertBarriers({ &preBarrier, 1 });

                        rhi::BufferCopyRegion region;
                        region.srcOffset = stagingAllocation->offset;
                        region.dstOffset = 0;
                        region.size = sizeof(sceneConstants);
                        cmdList.copyBuffer(stagingAllocation->buffer, storage.sceneConstantsBuffer.get(), { &region, 1 });

                        rhi::BufferBarrier postBarrier;
                        postBarrier.buffer = storage.sceneConstantsBuffer.get();
                        postBarrier.offset = 0;
                        postBarrier.size = rhi::kWholeBuffer;
                        postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                        postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
                        cmdList.insertBarriers({ &postBarrier, 1 });

                        storage.sceneConstantsBufferInitialized = true;
                    }
                }

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "RadiosityTrace",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(RadiosityTrace, Compute))
                    );
                }
                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kRadiosityTrace_Bindings);
                }
                if (!storage.pipeline)
                {
                    storage.pipeline = createRadiosityComputePipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                }

                rhi::Buffer* meshShapeData = context.getBuffer(data.meshShapeData);
                rhi::Buffer* meshInstanceData = context.getBuffer(data.meshInstanceData);
                rhi::Buffer* gdfIndirection = context.getBuffer(data.gdfIndirection);
                const rhi::TextureView* gdfAtlas = context.getTexture(data.gdfAtlas);
                const rhi::TextureView* cardNormalAtlas = context.getTexture(data.cardNormalAtlas);
                const rhi::TextureView* cardDepthAtlas = context.getTexture(data.cardDepthAtlas);
                rhi::Buffer* objectGrid = context.getBuffer(data.objectGrid);
                rhi::Buffer* cardPages = context.getBuffer(data.cardPages);
                const rhi::TextureView* cardFinalLightingAtlas = context.getTexture(data.cardFinalLightingAtlas);
                const rhi::TextureView* cardAlbedoAtlas = context.getTexture(data.cardAlbedoAtlas);
                const rhi::TextureView* cardEmissiveAtlas = context.getTexture(data.cardEmissiveAtlas);
                const rhi::TextureView* cardDirectLightingAtlas = context.getTexture(data.cardDirectLightingAtlas);
                const rhi::TextureView* cardIndirectLightingAtlas = context.getTexture(data.cardIndirectLightingAtlas);
                const rhi::TextureView* skyLightPrefilter = context.getTexture(data.skyLightPrefilter);
                rhi::Buffer* skyLightData = context.getBuffer(data.skyLightData);
                const rhi::TextureView* traceRadianceAtlas = context.getTexture(data.traceRadianceAtlas);
                const rhi::TextureView* blueNoiseVec2Texture = context.getTexture(data.blueNoiseVec2Texture);
                rhi::Sampler* linearSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (storage.bindGroupCache.needUpdate(
                        meshShapeData, meshInstanceData, gdfIndirection, gdfAtlas, linearSampler,
                        cardNormalAtlas, cardDepthAtlas, objectGrid, cardPages, cardFinalLightingAtlas,
                        cardAlbedoAtlas, storage.sceneConstantsBuffer.get(), skyLightPrefilter, traceRadianceAtlas,
                        cardEmissiveAtlas, cardDirectLightingAtlas, cardIndirectLightingAtlas, blueNoiseVec2Texture,
                        skyLightData))
                {
                    storage.bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kRadiosityTrace_Bindings);
                            writer.setBuffer("MeshShapeData", meshShapeData);
                            writer.setBuffer("MeshInstanceDataBuffer", meshInstanceData);
                            writer.setBuffer("GdfIndirection", gdfIndirection);
                            writer.setTexture("GdfAtlas", gdfAtlas);
                            writer.setSampler("LinearSampler", linearSampler);
                            writer.setTexture("CardNormalAtlas", cardNormalAtlas);
                            writer.setTexture("CardDepthAtlas", cardDepthAtlas);
                            writer.setBuffer("ObjectGrid", objectGrid);
                            writer.setBuffer("CardPages", cardPages);
                            writer.setTexture("CardFinalLightingAtlas", cardFinalLightingAtlas);
                            writer.setTexture("CardAlbedoAtlas", cardAlbedoAtlas);
                            writer.setBuffer("SceneConstants", storage.sceneConstantsBuffer.get());
                            writer.setTexture("SkyLightPrefilter", skyLightPrefilter);
                            writer.setTexture("TraceRadianceAtlas", traceRadianceAtlas);
                            writer.setTexture("CardEmissiveAtlas", cardEmissiveAtlas);
                            writer.setTexture("CardDirectLightingAtlas", cardDirectLightingAtlas);
                            writer.setTexture("CardIndirectLightingAtlas", cardIndirectLightingAtlas);
                            writer.setTexture("BlueNoiseVec2Texture", blueNoiseVec2Texture);
                            writer.setBuffer("SkyLightData", skyLightData);
                            writer.apply(bindGroup);
                        }
                    );
                }

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    1, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(
                    2, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    storage.bindGroupCache.getBindGroup(), {});
                cmdList.setBindGroup(
                    3, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    data.instanceDataBindGroup, {});

                for (const RadiosityTracePageTask& task : data.tasks)
                {
                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(), 0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&task.constants), sizeof(task.constants)));
                    cmdList.dispatch(task.groupsX, task.groupsY, 1);
                }
            }
        );
    }

    void RadiosityPass::addFilterPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const CameraProxy& cameraProxy
    )
    {
        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
            return;
        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();

        // 只有 [A] Trace 真正跑过(没有早退)才会写这个 key——理由跟 addConvertToSHPass 顶部一致
        auto traceRadianceAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("RadiosityTraceRadianceAtlas"_hs);
        if (!traceRadianceAtlasHandle.isValid())
            return;

        std::vector<RadiosityFilterPageTask> tasks;
        for (const SurfaceCacheLightingPage& page : lumenScene->getLightingSchedule().indirect)
        {
            const MeshCard& card = page.proxy->getMeshShapeProxy()->getCards()[page.cardIndex];

            RadiosityFilterPageTask task;
            task.constants = RadiosityFilterConstants{};
            task.constants.cardCenterX = card.center.x;
            task.constants.cardCenterY = card.center.y;
            task.constants.cardCenterZ = card.center.z;
            task.constants.cardHalfExtentsX = card.halfExtents.x;
            task.constants.cardHalfExtentsY = card.halfExtents.y;
            task.constants.cardHalfExtentsZ = card.halfExtents.z;
            task.constants.directionIndex = card.directionIndex;
            task.constants.packedAddress = page.packedAddress;
            task.constants.pageIndexX = page.pageIndexX;
            task.constants.pageIndexY = page.pageIndexY;
            task.constants.pageCountX = page.pageCountX;
            task.constants.pageCountY = page.pageCountY;
            task.constants.pageIndirectionOffset = page.pageIndirectionOffset;
            task.constants.instanceIndex = page.proxy->getInstanceIndex();
            task.constants.temporalIndex = page.indirectTemporalIndex;
            task.groupsX = (uint32_t(page.extent.x) / 4 + 7) / 8;
            task.groupsY = (uint32_t(page.extent.y) / 4 + 7) / 8;
            tasks.push_back(task);
        }

        if (tasks.empty())
            return;

        auto cardNormalAtlasHandle = renderGraph.importTexture(
            "CardAtlasNormal"_hs, &surfaceCacheAtlas.getNormalAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto cardDepthAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasDepth"_hs, [&]
        {
            return renderGraph.importTexture("CardAtlasDepth"_hs, &surfaceCacheAtlas.getDepthAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        });
        auto cardPagesHandle = getOrImportResource<rg::BufferHandle>(renderGraph, "MeshSDFCardPages"_hs, [&]
        {
            return renderGraph.importBuffer("MeshSDFCardPages"_hs, surfaceCacheAtlas.getCardPageTableBuffer());
        });

        renderGraph.addComputePass<RadiosityFilterData>(
            "RadiosityFilter"_hs,
            [&](rg::RenderGraph::Builder& builder, RadiosityFilterData& data)
            {
                builder.markSideEffect();

                data.tasks = std::move(tasks);
                data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(cameraProxy.getViewId());
                data.cardNormalAtlas = builder.sample(cardNormalAtlasHandle);
                data.cardDepthAtlas = builder.sample(cardDepthAtlasHandle);
                data.traceRadianceAtlas = builder.sample(traceRadianceAtlasHandle);
                data.cardPages = builder.accessBuffer(cardPagesHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);

                rhi::TextureDesc filteredDesc;
                filteredDesc.type = rhi::TextureType::Texture_2D;
                filteredDesc.format = rhi::TextureFormat::B10G11R11_UFLOAT_PACK32;
                filteredDesc.extent = uint3(
                    uint32_t(kSurfaceCacheAtlasResolution),
                    uint32_t(kSurfaceCacheAtlasResolution),
                    1);
                filteredDesc.mipCount = 1;
                filteredDesc.layerCount = 1;
                filteredDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
                auto filteredTraceRadianceAtlas = builder.createTexture("RadiosityFilteredTraceRadianceAtlas"_hs, filteredDesc);
                data.filteredTraceRadianceAtlas = builder.storage(filteredTraceRadianceAtlas, rg::AccessType::Write_Only);

                renderGraph.getBlackboard().put("RadiosityFilteredTraceRadianceAtlas"_hs, data.filteredTraceRadianceAtlas);
            },
            [&rendererContext](rg::ExecuteContext& context, RadiosityFilterData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<RadiosityPass, RadiosityFilterStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "RadiosityFilter",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(RadiosityFilter, Compute))
                    );
                }
                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kRadiosityFilter_Bindings);
                }
                if (!storage.pipeline)
                {
                    storage.pipeline = createRadiosityComputePipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                }

                const rhi::TextureView* cardNormalAtlas = context.getTexture(data.cardNormalAtlas);
                const rhi::TextureView* cardDepthAtlas = context.getTexture(data.cardDepthAtlas);
                const rhi::TextureView* traceRadianceAtlas = context.getTexture(data.traceRadianceAtlas);
                rhi::Buffer* cardPages = context.getBuffer(data.cardPages);
                const rhi::TextureView* filteredTraceRadianceAtlas = context.getTexture(data.filteredTraceRadianceAtlas);

                if (storage.bindGroupCache.needUpdate(cardNormalAtlas, cardDepthAtlas, traceRadianceAtlas, cardPages, filteredTraceRadianceAtlas))
                {
                    storage.bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kRadiosityFilter_Bindings);
                            writer.setTexture("CardNormalAtlas", cardNormalAtlas);
                            writer.setTexture("CardDepthAtlas", cardDepthAtlas);
                            writer.setTexture("TraceRadianceAtlas", traceRadianceAtlas);
                            writer.setBuffer("CardPages", cardPages);
                            writer.setTexture("FilteredTraceRadianceAtlas", filteredTraceRadianceAtlas);
                            writer.apply(bindGroup);
                        }
                    );
                }

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    1, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(
                    2, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    storage.bindGroupCache.getBindGroup(), {});
                cmdList.setBindGroup(
                    3, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    data.instanceDataBindGroup, {});

                for (const RadiosityFilterPageTask& task : data.tasks)
                {
                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(), 0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&task.constants), sizeof(task.constants)));
                    cmdList.dispatch(task.groupsX, task.groupsY, 1);
                }
            }
        );
    }

    void RadiosityPass::addConvertToSHPass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const CameraProxy& cameraProxy
    )
    {
        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
            return;
        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();

        // 只有 [B] SpatialFilter 真正跑过(没有早退)才会写这个 key——理由跟 addFilterPass 顶部一致
        auto filteredTraceRadianceAtlasHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("RadiosityFilteredTraceRadianceAtlas"_hs);
        if (!filteredTraceRadianceAtlasHandle.isValid())
            return;

        std::vector<RadiosityConvertToSHPageTask> tasks;
        for (const SurfaceCacheLightingPage& page : lumenScene->getLightingSchedule().indirect)
        {
            const MeshCard& card = page.proxy->getMeshShapeProxy()->getCards()[page.cardIndex];

            RadiosityConvertToSHPageTask task;
            task.constants = RadiosityConvertToSHConstants{};
            task.constants.cardCenterX = card.center.x;
            task.constants.cardCenterY = card.center.y;
            task.constants.cardCenterZ = card.center.z;
            task.constants.cardHalfExtentsX = card.halfExtents.x;
            task.constants.cardHalfExtentsY = card.halfExtents.y;
            task.constants.cardHalfExtentsZ = card.halfExtents.z;
            task.constants.directionIndex = card.directionIndex;
            task.constants.packedAddress = page.packedAddress;
            task.constants.pageIndexX = page.pageIndexX;
            task.constants.pageIndexY = page.pageIndexY;
            task.constants.pageCountX = page.pageCountX;
            task.constants.pageCountY = page.pageCountY;
            task.constants.instanceIndex = page.proxy->getInstanceIndex();
            task.constants.temporalIndex = page.indirectTemporalIndex;
            task.groupsX = (uint32_t(page.extent.x) / 4 + 7) / 8;
            task.groupsY = (uint32_t(page.extent.y) / 4 + 7) / 8;
            tasks.push_back(task);
        }

        if (tasks.empty())
            return;

        auto cardNormalAtlasHandle = renderGraph.importTexture(
            "CardAtlasNormal"_hs, &surfaceCacheAtlas.getNormalAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto cardDepthAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasDepth"_hs, [&]
        {
            return renderGraph.importTexture("CardAtlasDepth"_hs, &surfaceCacheAtlas.getDepthAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        });
        // 全程只读的引擎常驻资源, 跟 addTracePass 里 blueNoiseVec2Handle 同一个理由独立 import
        auto blueNoiseVec2Handle = renderGraph.importTexture(
            "BlueNoiseVec2Texture"_hs, &rendererContext.getRenderBackend().getDefaultResources().getBlueNoiseVec2View(), rhi::ResourceState::Shader_Sampled);

        renderGraph.addComputePass<RadiosityConvertToSHData>(
            "RadiosityConvertToSH"_hs,
            [&](rg::RenderGraph::Builder& builder, RadiosityConvertToSHData& data)
            {
                builder.markSideEffect();

                data.tasks = std::move(tasks);
                data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(cameraProxy.getViewId());
                data.cardNormalAtlas = builder.sample(cardNormalAtlasHandle);
                data.cardDepthAtlas = builder.sample(cardDepthAtlasHandle);
                data.filteredTraceRadianceAtlas = builder.sample(filteredTraceRadianceAtlasHandle);
                data.blueNoiseVec2Texture = builder.sample(blueNoiseVec2Handle);

                const uint32_t probeAtlasExtent = uint32_t(kSurfaceCacheAtlasResolution) / 4u;
                rhi::TextureDesc probeSHDesc;
                probeSHDesc.type = rhi::TextureType::Texture_2D;
                probeSHDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                probeSHDesc.extent = uint3(probeAtlasExtent, probeAtlasExtent, 1);
                probeSHDesc.mipCount = 1;
                probeSHDesc.layerCount = 1;
                probeSHDesc.usage = rhi::TextureUsage::Sampled | rhi::TextureUsage::Storage;
                auto probeSHR = builder.createTexture("RadiosityProbeSHRAtlas"_hs, probeSHDesc);
                auto probeSHG = builder.createTexture("RadiosityProbeSHGAtlas"_hs, probeSHDesc);
                auto probeSHB = builder.createTexture("RadiosityProbeSHBAtlas"_hs, probeSHDesc);
                data.probeSHRAtlas = builder.storage(probeSHR, rg::AccessType::Write_Only);
                data.probeSHGAtlas = builder.storage(probeSHG, rg::AccessType::Write_Only);
                data.probeSHBAtlas = builder.storage(probeSHB, rg::AccessType::Write_Only);

                renderGraph.getBlackboard().put("RadiosityProbeSHRAtlas"_hs, data.probeSHRAtlas);
                renderGraph.getBlackboard().put("RadiosityProbeSHGAtlas"_hs, data.probeSHGAtlas);
                renderGraph.getBlackboard().put("RadiosityProbeSHBAtlas"_hs, data.probeSHBAtlas);
            },
            [&rendererContext](rg::ExecuteContext& context, RadiosityConvertToSHData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<RadiosityPass, RadiosityConvertToSHStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "RadiosityConvertToSH",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(RadiosityConvertToSH, Compute))
                    );
                }
                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kRadiosityConvertToSH_Bindings);
                }
                if (!storage.pipeline)
                {
                    storage.pipeline = createRadiosityComputePipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                }

                const rhi::TextureView* cardNormalAtlas = context.getTexture(data.cardNormalAtlas);
                const rhi::TextureView* cardDepthAtlas = context.getTexture(data.cardDepthAtlas);
                const rhi::TextureView* filteredTraceRadianceAtlas = context.getTexture(data.filteredTraceRadianceAtlas);
                const rhi::TextureView* probeSHRAtlas = context.getTexture(data.probeSHRAtlas);
                const rhi::TextureView* probeSHGAtlas = context.getTexture(data.probeSHGAtlas);
                const rhi::TextureView* probeSHBAtlas = context.getTexture(data.probeSHBAtlas);
                const rhi::TextureView* blueNoiseVec2Texture = context.getTexture(data.blueNoiseVec2Texture);

                if (storage.bindGroupCache.needUpdate(cardNormalAtlas, cardDepthAtlas, filteredTraceRadianceAtlas, probeSHRAtlas, probeSHGAtlas, probeSHBAtlas, blueNoiseVec2Texture))
                {
                    storage.bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kRadiosityConvertToSH_Bindings);
                            writer.setTexture("CardNormalAtlas", cardNormalAtlas);
                            writer.setTexture("CardDepthAtlas", cardDepthAtlas);
                            writer.setTexture("FilteredTraceRadianceAtlas", filteredTraceRadianceAtlas);
                            writer.setTexture("ProbeSHRAtlas", probeSHRAtlas);
                            writer.setTexture("ProbeSHGAtlas", probeSHGAtlas);
                            writer.setTexture("ProbeSHBAtlas", probeSHBAtlas);
                            writer.setTexture("BlueNoiseVec2Texture", blueNoiseVec2Texture);
                            writer.apply(bindGroup);
                        }
                    );
                }

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    1, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(
                    2, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    storage.bindGroupCache.getBindGroup(), {});
                cmdList.setBindGroup(
                    3, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    data.instanceDataBindGroup, {});

                for (const RadiosityConvertToSHPageTask& task : data.tasks)
                {
                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(), 0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&task.constants), sizeof(task.constants)));
                    cmdList.dispatch(task.groupsX, task.groupsY, 1);
                }
            }
        );
    }

    void RadiosityPass::addIntegratePass(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const CameraProxy& cameraProxy
    )
    {
        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
            return;
        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();

        auto probeSHRHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("RadiosityProbeSHRAtlas"_hs);
        auto probeSHGHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("RadiosityProbeSHGAtlas"_hs);
        auto probeSHBHandle = renderGraph.getBlackboard().get<rg::TextureHandle>("RadiosityProbeSHBAtlas"_hs);
        if (!probeSHRHandle.isValid() || !probeSHGHandle.isValid() || !probeSHBHandle.isValid())
            return;

        std::vector<RadiosityIntegratePageTask> tasks;
        for (const SurfaceCacheLightingPage& page : lumenScene->getLightingSchedule().indirect)
        {
            const MeshCard& card = page.proxy->getMeshShapeProxy()->getCards()[page.cardIndex];

            RadiosityIntegratePageTask task;
            task.constants = RadiosityIntegrateConstants{};
            task.constants.cardCenterX = card.center.x;
            task.constants.cardCenterY = card.center.y;
            task.constants.cardCenterZ = card.center.z;
            task.constants.cardHalfExtentsX = card.halfExtents.x;
            task.constants.cardHalfExtentsY = card.halfExtents.y;
            task.constants.cardHalfExtentsZ = card.halfExtents.z;
            task.constants.directionIndex = card.directionIndex;
            task.constants.packedAddress = page.packedAddress;
            task.constants.pageIndexX = page.pageIndexX;
            task.constants.pageIndexY = page.pageIndexY;
            task.constants.pageCountX = page.pageCountX;
            task.constants.pageCountY = page.pageCountY;
            task.constants.pageIndirectionOffset = page.pageIndirectionOffset;
            task.constants.instanceIndex = page.proxy->getInstanceIndex();
            task.constants.temporalIndex = page.indirectTemporalIndex;
            task.groupsX = (uint32_t(page.extent.x) + 7) / 8;
            task.groupsY = (uint32_t(page.extent.y) + 7) / 8;
            tasks.push_back(task);
        }

        if (tasks.empty())
            return;

        auto cardNormalAtlasHandle = renderGraph.importTexture(
            "CardAtlasNormal"_hs, &surfaceCacheAtlas.getNormalAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        auto cardDepthAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasDepth"_hs, [&]
        {
            return renderGraph.importTexture("CardAtlasDepth"_hs, &surfaceCacheAtlas.getDepthAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        });
        auto cardPagesHandle = getOrImportResource<rg::BufferHandle>(renderGraph, "MeshSDFCardPages"_hs, [&]
        {
            return renderGraph.importBuffer("MeshSDFCardPages"_hs, surfaceCacheAtlas.getCardPageTableBuffer());
        });
        // IndirectLightingAtlas 常驻、下一帧 [A] Trace 反弹链路的一部分——不像 TraceRadiance/
        // ProbeSH 那样是帧内资源。用 getOrImportResource: 万一调用方之后还要采样这张图集
        // (比如可视化模式), 那份 import 必须复用这一份句柄, 见 CombineFinalLightingPass.cpp
        // 对 FinalLightingAtlas 同样的处理
        auto indirectLightingAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasIndirectLighting"_hs, [&]
        {
            return renderGraph.importTexture("CardAtlasIndirectLighting"_hs, &surfaceCacheAtlas.getIndirectLightingAtlasTextureView(), rhi::ResourceState::Shader_Sampled);
        });
        auto numFramesAccumulatedAtlasHandle = renderGraph.importTexture(
            "CardAtlasRadiosityNumFramesAccumulated"_hs, &surfaceCacheAtlas.getRadiosityNumFramesAccumulatedAtlasTextureView(), rhi::ResourceState::Shader_Sampled);

        renderGraph.addComputePass<RadiosityIntegrateData>(
            "RadiosityIntegrate"_hs,
            [&](rg::RenderGraph::Builder& builder, RadiosityIntegrateData& data)
            {
                builder.markSideEffect();

                data.tasks = std::move(tasks);
                data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(cameraProxy.getViewId());
                data.cardNormalAtlas = builder.sample(cardNormalAtlasHandle);
                data.cardDepthAtlas = builder.sample(cardDepthAtlasHandle);
                data.probeSHRAtlas = builder.sample(probeSHRHandle);
                data.probeSHGAtlas = builder.sample(probeSHGHandle);
                data.probeSHBAtlas = builder.sample(probeSHBHandle);
                data.cardPages = builder.accessBuffer(cardPagesHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.indirectLightingAtlas = builder.storage(indirectLightingAtlasHandle, rg::AccessType::Write_Only);
                data.numFramesAccumulatedAtlas = builder.storage(numFramesAccumulatedAtlasHandle, rg::AccessType::Read_Write);

                renderGraph.getBlackboard().put("CardAtlasIndirectLighting"_hs, data.indirectLightingAtlas);
            },
            [&rendererContext](rg::ExecuteContext& context, RadiosityIntegrateData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<RadiosityPass, RadiosityIntegrateStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "RadiosityIntegrate",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(RadiosityIntegrate, Compute))
                    );
                }
                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kRadiosityIntegrate_Bindings);
                }
                if (!storage.pipeline)
                {
                    storage.pipeline = createRadiosityComputePipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                }

                const rhi::TextureView* cardNormalAtlas = context.getTexture(data.cardNormalAtlas);
                const rhi::TextureView* cardDepthAtlas = context.getTexture(data.cardDepthAtlas);
                const rhi::TextureView* probeSHRAtlas = context.getTexture(data.probeSHRAtlas);
                const rhi::TextureView* probeSHGAtlas = context.getTexture(data.probeSHGAtlas);
                const rhi::TextureView* probeSHBAtlas = context.getTexture(data.probeSHBAtlas);
                rhi::Buffer* cardPages = context.getBuffer(data.cardPages);
                const rhi::TextureView* indirectLightingAtlas = context.getTexture(data.indirectLightingAtlas);
                const rhi::TextureView* numFramesAccumulatedAtlas = context.getTexture(data.numFramesAccumulatedAtlas);

                if (storage.bindGroupCache.needUpdate(cardNormalAtlas, cardDepthAtlas, probeSHRAtlas, probeSHGAtlas, probeSHBAtlas, cardPages, indirectLightingAtlas, numFramesAccumulatedAtlas))
                {
                    storage.bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kRadiosityIntegrate_Bindings);
                            writer.setTexture("RadiosityNumFramesAccumulatedAtlas", numFramesAccumulatedAtlas);
                            writer.setTexture("CardNormalAtlas", cardNormalAtlas);
                            writer.setTexture("CardDepthAtlas", cardDepthAtlas);
                            writer.setTexture("ProbeSHRAtlas", probeSHRAtlas);
                            writer.setTexture("ProbeSHGAtlas", probeSHGAtlas);
                            writer.setTexture("ProbeSHBAtlas", probeSHBAtlas);
                            writer.setBuffer("CardPages", cardPages);
                            writer.setTexture("IndirectLightingAtlas", indirectLightingAtlas);
                            writer.apply(bindGroup);
                        }
                    );
                }

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    1, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    data.viewBindGroup, { &data.viewDataDynamicOffset, 1 });
                cmdList.setBindGroup(
                    2, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    storage.bindGroupCache.getBindGroup(), {});
                cmdList.setBindGroup(
                    3, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(),
                    data.instanceDataBindGroup, {});

                for (const RadiosityIntegratePageTask& task : data.tasks)
                {
                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(), 0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&task.constants), sizeof(task.constants)));
                    cmdList.dispatch(task.groupsX, task.groupsY, 1);
                }
            }
        );
    }
}
