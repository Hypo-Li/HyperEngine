#include "CardDirectLightingPass.h"
#include "RenderPassUtils.h"
#include "../SurfaceCacheAtlas.h"
#include "../LumenScene.h"
#include "../CameraProxy.h"
#include "../GlobalDistanceField.h"
#include "../AtmosphereProxy.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/MeshShapeProxy.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>
#include <RenderBase/RenderBaseDefines.h>

#include <CardDirectLighting.generated.h>

#include <bit>
#include <cstring>
#include <vector>

namespace hyp
{
    namespace
    {
        // 跟 Shader/Material/CardDirectLighting.slang 里的 CardDirectLightingConstants
        // 逐字段对应, 必须手动保持同步——总大小压在 push constant 64 字节硬上限
        // (rhi::kImmediateDataSize), 手动按标量摆放不用 float3/int3, 理由跟
        // CardCapturePass.cpp/MeshSDFPass.cpp 的 push constant 一致
        struct CardDirectLightingConstants
        {
            float cardCenterX, cardCenterY, cardCenterZ;
            float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
            uint32_t directionIndex;
            // mCardPageTable 里存的打包值本身, 不是预先解码好的像素原点——shader 端用
            // decodeCardPageAddress() 自己解出像素原点+实际尺寸(子页分配需要知道实际尺寸,
            // 单独传 pixelExtentX/Y 又会超出 64 字节硬上限, 见 CardDirectLighting.slang
            // 里这个字段的注释)
            uint32_t packedAddress;
            uint32_t pageIndexX, pageIndexY;
            uint32_t pageCountX, pageCountY;
            uint32_t instanceIndex;
            int32_t gdfAtlasBrickGridExtent;
            int32_t sourceAtlasBrickGridExtent;
        };
        static_assert(sizeof(CardDirectLightingConstants) == 60);

        // 一次任务对应图集里的一页, 跟 CardCapturePass::CardPageTask 同一个粒度——这个 pass
        // 复用它已经算好的 card/页分配结果(不重新调用 updateMeshCardPages(), 那是
        // CardCapturePass 自己 builder 阶段的职责), 只是按 CardDirectLightingConstants
        // 重新打包一份 push constant
        struct CardDirectLightingPageTask
        {
            CardDirectLightingConstants constants;
            uint32_t groupsX;
            uint32_t groupsY;
        };

        struct CardDirectLightingData
        {
            std::vector<CardDirectLightingPageTask> tasks;
            const rhi::BindGroup* viewBindGroup;
            const rhi::BindGroup* instanceDataBindGroup;
            uint32_t viewDataDynamicOffset;
            rg::BufferHandle meshShapeData;
            rg::BufferHandle meshInstanceData;
            rg::TextureHandle sourceAtlas;
            rg::BufferHandle gdfIndirection;
            rg::TextureHandle gdfAtlas;
            rg::TextureHandle cardNormalAtlas;
            rg::TextureHandle cardDepthAtlas;
            // 不走 render graph 的 import/barrier 追踪——跟 DirectLightingPass.cpp 对
            // sLightData 的处理同一个理由: getLightDataBuffer() 返回 const 引用, 这份 buffer
            // 的写入(markLightDataUpdated 触发的上传)由帧开头的其它环节保证先于这里执行,
            // 不需要在这个 pass 内部重新建立读后写依赖
            const rhi::Buffer* lightData;
            rg::BufferHandle shadowTileGrid;
            rg::TextureHandle directLightingAtlas;
            bool hasAtmosphere;
            rg::TextureHandle transmittanceLut; // 只有 hasAtmosphere 时才有效
        };

        struct CardDirectLightingStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> bindGroupLayout;
            TRef<rhi::Pipeline> pipeline;
            BindGroupCache<14> bindGroupCache;

            // referenceOrigin(主相机世界位置) + lightCount, 每帧内容变但大小固定(8 个
            // uint32), 不需要 RangeAllocator——只建一次, 每帧原地覆盖上传
            TRef<rhi::Buffer> sceneConstantsBuffer;
            bool sceneConstantsBufferInitialized = false;
        };

        rhi::Pipeline* createCardDirectLightingPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout)
        {
            return createComputePipeline(rendererContext, shaderVariant, bindGroupLayout, &rendererContext.getInstanceDataBindGroupLayout());
        }
    }

    void CardDirectLightingPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const CameraProxy& cameraProxy,
        const AtmosphereProxy* atmosphere
    )
    {
        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
            return;
        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();

        RenderBackend& renderBackend = rendererContext.getRenderBackend();
        DistanceFieldBrickAtlas& sourceAtlasPool = renderBackend.getMeshDistanceFieldBrickAtlas();
        MeshShapeDataTable& objectTable = renderBackend.getMeshShapeDataTable();
        DistanceFieldBrickAtlas& gdfAtlasPool = lumenScene->getGlobalDistanceFieldBrickAtlas();
        GlobalDistanceField& gdf = cameraProxy.getGlobalDistanceField();

        // 数据没就绪就直接跳过这一帧——不像 MeshSDFPass/GlobalDistanceFieldVisualizerPass
        // 那样挂在屏幕上、有"没数据也要清屏"的义务, 这个 pass 只是往 DirectLightingAtlas
        // 里写, 跳过一帧等下一帧数据自然追上即可
        rhi::Buffer* gdfIndirectionBuffer = gdf.getIndirectionBuffer();
        if (!sourceAtlasPool.isInitialized()
            || objectTable.hasPendingUploads()
            || !gdfAtlasPool.isInitialized()
            || !gdfIndirectionBuffer)
        {
            return;
        }

        const int3 sourceBrickGridExtent = sourceAtlasPool.getBrickGridExtent();
        const int3 gdfBrickGridExtent = gdfAtlasPool.getBrickGridExtent();

        std::vector<CardDirectLightingPageTask> tasks;
        for (const SurfaceCacheLightingPage& page : lumenScene->getLightingSchedule().direct)
        {
            const MeshCard& card = page.proxy->getMeshShapeProxy()->getCards()[page.cardIndex];

            CardDirectLightingPageTask task;
            task.constants = CardDirectLightingConstants{};
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
            task.constants.gdfAtlasBrickGridExtent = gdfBrickGridExtent.x;
            task.constants.sourceAtlasBrickGridExtent = sourceBrickGridExtent.x;
            task.groupsX = (uint32_t(page.extent.x) + 7) / 8;
            task.groupsY = (uint32_t(page.extent.y) + 7) / 8;
            tasks.push_back(task);
        }

        if (tasks.empty())
            return;

        // referenceOrigin 固定用主相机(跟点/聚光阴影用的 GDF、方向光 tile grid 用的
        // referenceOrigin 是同一个相机, 调用方 SceneProxy::update()/
        // GlobalDistanceFieldVisualizerPass 都已经统一成这一个), lightCount 是场景当前
        // 全部灯光数(不做剔除, 见 Shader 顶部注释)
        // 必须读 LumenScene 存下来的这份值, 不能自己重新调用 cameraProxies[0]->getWorldPosition()
        // 再算一次——GPU 端投影用的 referenceOrigin 要跟 CPU 建 tile grid(LumenScene::
        // updateDirectLightingShadowCandidates())时用的完全是同一个值, 两次独立读数没有
        // 任何机制保证一致, 见 getDirectLightingReferenceOrigin() 的注释
        const WorldCoord& referenceOrigin = lumenScene->getDirectLightingReferenceOrigin();
        uint32_t lightCount = scene.getLightCount();

        const rhi::Buffer* lightDataBuffer = &scene.getLightDataBuffer();
        auto shadowTileGridHandle = renderGraph.importBuffer(
            "DirectLightingShadowTileGrid"_hs(scene.getSceneId()),
            lumenScene->getDirectLightingShadowTileGridBuffer()
        );
        auto meshShapeDataHandle = getOrImportResource<rg::BufferHandle>(renderGraph, "MeshShapeDataTable"_hs, [&]
        {
            return renderGraph.importBuffer("MeshShapeDataTable"_hs, objectTable.getBuffer());
        });
        HString meshInstanceDataKey = "SceneMeshInstanceData"_hs(scene.getSceneId());
        auto meshInstanceDataHandle = getOrImportResource<rg::BufferHandle>(renderGraph, meshInstanceDataKey, [&]
        {
            return renderGraph.importBuffer(meshInstanceDataKey, &scene.getMeshInstanceDataBuffer());
        });
        auto sourceAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "SparseBrickAtlas"_hs, [&]
        {
            return renderGraph.importTexture(
                "SparseBrickAtlas"_hs,
                &sourceAtlasPool.getAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
        });
        HString gdfIndirectionKey = "GlobalDistanceFieldIndirection"_hs(cameraProxy.getViewId());
        auto gdfIndirectionHandle = getOrImportResource<rg::BufferHandle>(renderGraph, gdfIndirectionKey, [&]
        {
            return renderGraph.importBuffer(gdfIndirectionKey, gdfIndirectionBuffer);
        });
        HString gdfAtlasKey = "GlobalDistanceFieldAtlas"_hs(cameraProxy.getViewId());
        auto gdfAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, gdfAtlasKey, [&]
        {
            return renderGraph.importTexture(
                gdfAtlasKey,
                &gdfAtlasPool.getAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
        });
        auto cardNormalAtlasHandle = renderGraph.importTexture(
            "CardAtlasNormal"_hs,
            &surfaceCacheAtlas.getNormalAtlasTextureView(),
            rhi::ResourceState::Shader_Sampled
        );
        auto cardDepthAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasDepth"_hs, [&]
        {
            return renderGraph.importTexture(
                "CardAtlasDepth"_hs,
                &surfaceCacheAtlas.getDepthAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
        });
        // "CardAtlasDirectLighting"_hs 这个 key 已经被调用方 GlobalDistanceFieldVisualizerPass
        // 导入过一次(它自己也要采样这张图集显示结果)——同一份物理资源不能各自独立 import
        // 两次, 见 getOrImportResource() 的注释, 这里必须复用那一份句柄
        auto directLightingAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "CardAtlasDirectLighting"_hs, [&]
        {
            return renderGraph.importTexture(
                "CardAtlasDirectLighting"_hs,
                &surfaceCacheAtlas.getDirectLightingAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
        });

        renderGraph.addComputePass<CardDirectLightingData>(
            "CardDirectLighting"_hs,
            [&](rg::RenderGraph::Builder& builder, CardDirectLightingData& data)
            {
                builder.markSideEffect();

                data.tasks = std::move(tasks);
                // 这个 pass 要读 sInstanceData(set1, binding0)——虽然不需要 uViewData 那份
                // 逐 view 数据, 但两者共用同一个 bind group, 必须显式重绑(不能依赖"Prepare"
                // pass 留下的隐式状态), 跟 GlobalDistanceFieldCompositePass 同一个理由
                data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(cameraProxy.getViewId());
                data.meshShapeData = builder.accessBuffer(meshShapeDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.meshInstanceData = builder.accessBuffer(meshInstanceDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.sourceAtlas = builder.sample(sourceAtlasHandle);
                data.gdfIndirection = builder.accessBuffer(gdfIndirectionHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.gdfAtlas = builder.sample(gdfAtlasHandle);
                data.cardNormalAtlas = builder.sample(cardNormalAtlasHandle);
                data.cardDepthAtlas = builder.sample(cardDepthAtlasHandle);
                data.lightData = lightDataBuffer;
                data.shadowTileGrid = builder.accessBuffer(shadowTileGridHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.directLightingAtlas = builder.storage(directLightingAtlasHandle, rg::AccessType::Write_Only);
                // 调用方(GlobalDistanceFieldVisualizerPass)之后还要采样这张图集显示结果——
                // 必须拿到这次写完之后的新版本句柄它的 builder.sample() 才会被识别成读后写
                // 依赖、自动插入 barrier, 见函数末尾把这个新版本存回 blackboard 的注释
                directLightingAtlasHandle = data.directLightingAtlas;

                // 跟 DirectLightingPass.cpp 同一套"有没有大气"处理: 没有大气时不 import 这张
                // LUT(场景可能根本没建过 AtmospherePass), execute 阶段绑 DefaultResources 的
                // 占位资源
                data.hasAtmosphere = atmosphere != nullptr;
                if (data.hasAtmosphere)
                {
                    auto transmittanceLutHandle = builder.getBlackboard().get<rg::TextureHandle>("TransmittanceLut"_hs(scene.getSceneId()));
                    data.transmittanceLut = builder.sample(transmittanceLutHandle);
                }
            },
            [&rendererContext, &scene, lumenScene, referenceOrigin, lightCount, atmosphere](rg::ExecuteContext& context, CardDirectLightingData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<CardDirectLightingPass, CardDirectLightingStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                // 方向光 tile grid 共享 buffer 的上传——必须在这个 pass 真正读它之前 flush,
                // 放在同一个 execute 回调开头, 跟 CardCapturePass::execute() 里 flush
                // card page table upload 是同一个理由
                lumenScene->prepareDirectLightingShadowTileGridUpload(cmdList);

                if (!storage.sceneConstantsBuffer)
                {
                    rhi::BufferDesc bufferDesc;
                    bufferDesc.size = 8 * sizeof(uint32_t);
                    bufferDesc.usage = rhi::BufferUsage::Storage | rhi::BufferUsage::Transfer_Dst;
                    storage.sceneConstantsBuffer = renderBackend.getDevice().createBuffer(bufferDesc, rhi::MemoryType::Gpu_Only);
                    storage.sceneConstantsBuffer->setDebugName("CardDirectLightingPass.SceneConstants");
                }

                // referenceOrigin(3 个 tile + 3 个 offset) + lightCount + 保留字——每帧
                // 内容都可能变(相机移动/灯光数量变化), 固定 8 字, 每帧整个原地覆盖上传,
                // 不需要 RangeAllocator
                {
                    uint32_t sceneConstants[8];
                    sceneConstants[0] = static_cast<uint32_t>(referenceOrigin.tile.x);
                    sceneConstants[1] = static_cast<uint32_t>(referenceOrigin.tile.y);
                    sceneConstants[2] = static_cast<uint32_t>(referenceOrigin.tile.z);
                    sceneConstants[3] = std::bit_cast<uint32_t>(referenceOrigin.offset.x);
                    sceneConstants[4] = std::bit_cast<uint32_t>(referenceOrigin.offset.y);
                    sceneConstants[5] = std::bit_cast<uint32_t>(referenceOrigin.offset.z);
                    sceneConstants[6] = lightCount;
                    sceneConstants[7] = 0;

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
                    // staging 预算耗尽就跳过——这一帧用上一帧的旧内容(相机位置/光源数量一帧
                    // 的滞后不会造成明显错误), 下一帧自然重试
                }

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "CardDirectLighting",
                        makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(CardDirectLighting, Compute))
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kCardDirectLighting_Bindings);
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createCardDirectLightingPipeline(rendererContext, storage.shader->getVariant(), *storage.bindGroupLayout);
                }

                rhi::Buffer* meshShapeData = context.getBuffer(data.meshShapeData);
                rhi::Buffer* meshInstanceData = context.getBuffer(data.meshInstanceData);
                const rhi::TextureView* sourceAtlas = context.getTexture(data.sourceAtlas);
                rhi::Buffer* gdfIndirection = context.getBuffer(data.gdfIndirection);
                const rhi::TextureView* gdfAtlas = context.getTexture(data.gdfAtlas);
                const rhi::TextureView* cardNormalAtlas = context.getTexture(data.cardNormalAtlas);
                const rhi::TextureView* cardDepthAtlas = context.getTexture(data.cardDepthAtlas);
                const rhi::Buffer* lightData = data.lightData;
                rhi::Buffer* shadowTileGrid = context.getBuffer(data.shadowTileGrid);
                const rhi::TextureView* directLightingAtlas = context.getTexture(data.directLightingAtlas);
                rhi::Sampler* linearSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                // 场景没有大气时绑 DefaultResources 的通用占位资源(全零 buffer + 全白纹理)。
                // CardDirectLighting.slang 里靠 topRadiusKm<=bottomRadiusKm(全零时天然成立)
                // 判断出"没有真实大气", 跳过透过率计算, 不依赖占位资源里有什么具体内容——跟
                // DirectLightingPass.cpp 同一个理由
                auto& defaultResources = renderBackend.getDefaultResources();
                rhi::Buffer* atmosphereParameters = data.hasAtmosphere
                    ? &atmosphere->getParametersBuffer()
                    : &defaultResources.getDummyUniformBuffer();
                const rhi::TextureView* transmittanceLut = data.hasAtmosphere
                    ? context.getTexture(data.transmittanceLut)
                    : &defaultResources.getWhiteTextureView();

                if (storage.bindGroupCache.needUpdate(
                        meshShapeData, meshInstanceData, sourceAtlas, gdfIndirection, gdfAtlas, linearSampler,
                        cardNormalAtlas, cardDepthAtlas, lightData,
                        storage.sceneConstantsBuffer.get(), shadowTileGrid, directLightingAtlas,
                        atmosphereParameters, transmittanceLut))
                {
                    storage.bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kCardDirectLighting_Bindings);
                            writer.setBuffer("MeshShapeData", meshShapeData);
                            writer.setBuffer("MeshInstanceDataBuffer", meshInstanceData);
                            writer.setTexture("SourceBrickAtlas", sourceAtlas);
                            writer.setBuffer("GdfIndirection", gdfIndirection);
                            writer.setTexture("GdfAtlas", gdfAtlas);
                            writer.setSampler("LinearSampler", linearSampler);
                            writer.setTexture("CardNormalAtlas", cardNormalAtlas);
                            writer.setTexture("CardDepthAtlas", cardDepthAtlas);
                            writer.setBuffer("sLightData", lightData);
                            writer.setBuffer("SceneConstants", storage.sceneConstantsBuffer.get());
                            writer.setBuffer("Atmosphere", atmosphereParameters, 0, sizeof(AtmosphereShaderParameters));
                            writer.setTexture("TransmittanceLutTexture", transmittanceLut);
                            writer.setBuffer("DirectLightingShadowTileGridBuffer", shadowTileGrid);
                            writer.setTexture("DirectLightingAtlas", directLightingAtlas);
                            writer.apply(bindGroup);
                        }
                    );
                }

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    1,
                    rhi::PipelineBindPoint::Compute,
                    &storage.pipeline->getLayout(),
                    data.viewBindGroup,
                    { &data.viewDataDynamicOffset, 1 }
                );
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Compute,
                    &storage.pipeline->getLayout(),
                    storage.bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.setBindGroup(3, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(), data.instanceDataBindGroup, {});

                for (const CardDirectLightingPageTask& task : data.tasks)
                {
                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(),
                        0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&task.constants), sizeof(task.constants))
                    );
                    cmdList.dispatch(task.groupsX, task.groupsY, 1);
                }
            }
        );

        // 存回去的是写完之后的新版本——GlobalDistanceFieldVisualizerPass 从 blackboard 里
        // 查到的必须是这个版本, 它对这张纹理的 builder.sample() 才会被 render graph 正确
        // 识别成读后写依赖、自动插入 barrier, 不是被当成一次跟这里的写入无关的独立首次访问,
        // 跟 GlobalDistanceFieldCompositePass.cpp 对 outputAtlasKey 的处理同一个理由
        renderGraph.getBlackboard().put("CardAtlasDirectLighting"_hs, directLightingAtlasHandle);
    }
}
