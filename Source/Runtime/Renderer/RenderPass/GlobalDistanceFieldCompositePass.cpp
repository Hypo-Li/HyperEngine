#include "GlobalDistanceFieldCompositePass.h"
#include "RenderPassUtils.h"

#include <RenderBase/RenderBaseDefines.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>
#include "../CameraProxy.h"
#include "../GlobalDistanceField.h"
#include "../LumenScene.h"

#include <Core/Common/LogSystem.h>

#include <GlobalDistanceFieldComposite.generated.h>

#include <array>
#include <bit>

namespace hyp
{
    namespace
    {
        // 跟 GlobalDistanceFieldComposite.slang 里的 GlobalDistanceFieldCompositePushConstants
        // 逐字段对应——cascade 专属的 gridMin/cellSize/staticLayerIndirectionOffset 不在这里,
        // 那些数据挪进了 GlobalDistanceFieldCompositeData::indicesData 里的 cascade 信息表,
        // 见下面 addToRenderGraph 的注释
        struct GlobalDistanceFieldCompositePushConstants
        {
            int32_t sourceAtlasBrickGridExtent;
            int32_t outputAtlasBrickGridExtent;
            int32_t mergeStaticLayer;
        };
        static_assert(sizeof(GlobalDistanceFieldCompositePushConstants) == 3 * sizeof(uint32_t));

        struct GlobalDistanceFieldCompositeData
        {
            uint32_t viewDataDynamicOffset;
            const rhi::BindGroup* viewBindGroup;
            const rhi::BindGroup* drawBindGroup;
            rg::BufferHandle objectTable;
            rg::BufferHandle gdfIndirection;
            rg::BufferHandle meshInstanceData;
            rg::BufferHandle objectGrid;
            rg::TextureHandle sourceAtlas;
            rg::TextureHandle outputAtlas;
            GlobalDistanceFieldCompositePushConstants constants;

            // 静态/动态各自一份, 拼好之后原样传给 execute 阶段上传进全局共享的 indices buffer——
            // builder 回调是唯一知道"这一批到底有哪些层要合成"的地方, execute 阶段只管上传
            // 和 dispatch, 不重新做拼装
            std::vector<uint32_t> indicesData;
            uint32_t totalBrickCount;
        };

        struct GlobalDistanceFieldCompositeStorage
        {
            std::shared_ptr<PassShader> shader;
            TRef<rhi::BindGroupLayout> passLayout;
            TRef<rhi::Pipeline> pipeline;
            // 0=静态 pass, 1=动态 pass——两个 pass 各自的 bind group 内容(除了 outputAtlas
            // 的 render graph 版本号)不会变, 用 constants.mergeStaticLayer 直接当下标
            std::array<BindGroupCache<7>, 2> bindGroupCaches;
        };

        struct GlobalDistanceFieldCompositePrepareData
        {
        };

        struct PendingLayer
        {
            GlobalDistanceField::Cascade* cascade;
            GlobalDistanceField::Layer* layer;
            int32_t cascadeIndex;
        };
    }

    void GlobalDistanceFieldCompositePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const CameraProxy& cameraProxy)
    {
        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
            return;

        RenderBackend& renderBackend = rendererContext.getRenderBackend();
        DistanceFieldBrickAtlas& sourceAtlasPool = renderBackend.getMeshDistanceFieldBrickAtlas();
        MeshShapeDataTable& objectTable = renderBackend.getMeshShapeDataTable();
        DistanceFieldBrickAtlas& outputAtlasPool = lumenScene->getGlobalDistanceFieldBrickAtlas();

        // 前两个(per-mesh 源 atlas、MeshShapeDataTable)由 RenderBackend 拥有、跨场景共享,
        // 不属于任何一个 Pass 独占; 后两个(GDF 输出 atlas、这个相机自己的间接索引表)
        // 现在也收在这里一起管, 因为这个 pass 是它们目前唯一的消费方。不管这一帧这个
        // 相机的 GDF 最终有没有 cascade 需要合成, 都要照常推进这几个 flush(否则数据永远
        // 传不完), 跟 MeshSDFPass::addToRenderGraph() 里 MeshSDFPrepare 的注释是同一个理由,
        // 因此放在下面 pendingStaticLayers/pendingDynamicLayers 为空就提前 return 之前无条件
        // 加入。四个 prepare() 都是幂等的, 被多个相机各自调一次没有问题
        renderGraph.addTransferPass<GlobalDistanceFieldCompositePrepareData>(
            "GlobalDistanceFieldCompositePrepare"_hs,
            [](rg::RenderGraph::Builder& builder, GlobalDistanceFieldCompositePrepareData&)
            {
                builder.markSideEffect();
            },
            [&rendererContext, &outputAtlasPool, &cameraProxy](rg::ExecuteContext& context, GlobalDistanceFieldCompositePrepareData&)
            {
                auto& cmdList = context.getCommandList();
                rendererContext.getRenderBackend().getMeshDistanceFieldBrickAtlas().prepare(cmdList);
                rendererContext.getRenderBackend().getMeshShapeDataTable().prepare(cmdList);
                outputAtlasPool.prepare(cmdList);
                cameraProxy.getGlobalDistanceField().prepare(cmdList);
            }
        );

        GlobalDistanceField& gdf = cameraProxy.getGlobalDistanceField();

        // cascade 中心重定心 + 候选剔选/brick 分配——这份 CPU 决策只有这个 pass(下面马上
        // 要读 needsComposite 决定要不要合成)关心, 必须在那之前跑完
        {
            const double3 cameraWorldPos = cameraProxy.getWorldPosition();
            for (GlobalDistanceField::Cascade& cascade : gdf.getCascades())
            {
                // 中心按格子边长取整——相机移动没跨过一整格之前, snappedCenter 算出来
                // 还是同一个值, 天然等价于"移动超过阈值才重算", 不需要额外的距离比较
                const double cellSize = static_cast<double>(2.0f * cascade.halfExtent) / kGlobalDistanceFieldBricksPerAxis;
                const double3 snappedCenter = glm::round(cameraWorldPos / cellSize) * cellSize;
                if (snappedCenter != cascade.center)
                {
                    cascade.center = snappedCenter;
                    cascade.staticLayer.dirty = true;
                    cascade.dynamicLayer.dirty = true;
                }
            }
            gdf.update(scene.getMeshRenderProxies(), outputAtlasPool);
        }

        std::vector<PendingLayer> pendingStaticLayers;
        std::vector<PendingLayer> pendingDynamicLayers;
        for (int32_t cascadeIndex = 0; cascadeIndex < kGlobalDistanceFieldCascadeCount; ++cascadeIndex)
        {
            GlobalDistanceField::Cascade& cascade = gdf.getCascades()[cascadeIndex];
            if (cascade.staticLayer.needsComposite)
                pendingStaticLayers.push_back({ &cascade, &cascade.staticLayer, cascadeIndex });
            if (cascade.dynamicLayer.needsComposite)
                pendingDynamicLayers.push_back({ &cascade, &cascade.dynamicLayer, cascadeIndex });
        }
        if (pendingStaticLayers.empty() && pendingDynamicLayers.empty())
            return;

        // 上面这一帧才刚发出 flush 请求, 请求本身能不能在这一帧的 staging 预算内实际完成
        // 是不确定的——这里查的是"上一次已经跑完的 prepare() 结果", 跟 MeshSDFPass 里
        // 同一处注释是同一个理由。没准备好就整体跳过, needsComposite 保持 true, 下一帧
        // 自然会重试
        if (!sourceAtlasPool.isInitialized() || objectTable.hasPendingUploads() || !outputAtlasPool.isInitialized())
            return;

        // 这几份资源也被 GlobalDistanceFieldVisualizerPass 用到——通过 blackboard 查有没有
        // 已经 import 过, 有就直接复用那份句柄, 不重新 import。render graph 的 import 不去重,
        // 同一份物理资源被两次 import() 会变成两个互不知情的独立资源节点, 各自的访问历史/
        // 自动 barrier 推导都看不到对方那一份, 见 getOrImportResource() 的注释
        auto sourceAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "SparseBrickAtlas"_hs, [&]
        {
            return renderGraph.importTexture(
                "SparseBrickAtlas"_hs,
                &sourceAtlasPool.getAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
        });
        auto objectTableHandle = getOrImportResource<rg::BufferHandle>(renderGraph, "MeshShapeDataTable"_hs, [&]
        {
            return renderGraph.importBuffer("MeshShapeDataTable"_hs, objectTable.getBuffer());
        });
        // GDF 自己那份"cascade 格子 -> atlas 槽位号"间接索引表——动态层合成时用它查同一个
        // cellIndex 静态层有没有 brick(见 GlobalDistanceFieldComposite.slang 顶部注释)。
        // 走到这里 pendingStaticLayers/pendingDynamicLayers 至少有一个非空, 意味着至少
        // 有一层刚成功 queueLayerIndirectionUpload 过, mIndirectionBuffer 保证已经懒创建
        HString gdfIndirectionKey = "GlobalDistanceFieldIndirection"_hs(cameraProxy.getViewId());
        auto gdfIndirectionHandle = getOrImportResource<rg::BufferHandle>(renderGraph, gdfIndirectionKey, [&]
        {
            return renderGraph.importBuffer(gdfIndirectionKey, gdf.getIndirectionBuffer());
        });
        HString meshInstanceDataKey = "SceneMeshInstanceData"_hs(scene.getSceneId());
        auto meshInstanceDataHandle = getOrImportResource<rg::BufferHandle>(renderGraph, meshInstanceDataKey, [&]
        {
            return renderGraph.importBuffer(meshInstanceDataKey, &scene.getMeshInstanceDataBuffer());
        });
        HString objectGridKey = "GlobalDistanceFieldObjectGrid"_hs(cameraProxy.getViewId());
        rg::BufferHandle objectGridHandle = getOrImportResource<rg::BufferHandle>(renderGraph, objectGridKey, [&]
        {
            return renderGraph.importBuffer(objectGridKey, gdf.getObjectGridBuffer());
        });
        HString outputAtlasKey = "GlobalDistanceFieldAtlas"_hs(cameraProxy.getViewId());
        rg::TextureHandle outputAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, outputAtlasKey, [&]
        {
            return renderGraph.importTexture(
                outputAtlasKey,
                &outputAtlasPool.getAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
        });

        const int3 sourceBrickGridExtent = sourceAtlasPool.getBrickGridExtent();
        const int3 outputBrickGridExtent = outputAtlasPool.getBrickGridExtent();

        // 静态层跟动态层分开两个 pass(不是同一个 pass 里循环两遍 dispatch): 动态层合成要
        // 读静态层刚写完的结果, 两者对同一张 atlas 纹理有真实的读后写依赖, 必须让 render
        // graph 通过 builder.storage() 返回的新版本 handle 建立依赖链, 自动插入正确的
        // barrier——不能在一个 pass 内部靠手写顺序隐式保证, 那样等于绕开 render graph 自己
        // 管 barrier 这件事。静态层自己合成不需要这一步(它不合并任何更早的东西)
        //
        // 每个 pass 内部不再逐层循环 dispatch——把这一批待合成的全部 cascade 拼成一份数据
        // (cascade 信息表 + 合并后的 brick header + 合并后的 payload), 一次上传进全局共享
        // 的 sIndices, 一次 dispatch 处理这一批全部 brick
        auto addCompositePass = [&](HString passName, bool mergeStaticLayer, const std::vector<PendingLayer>& layers)
        {
            if (layers.empty())
                return;

            renderGraph.addComputePass<GlobalDistanceFieldCompositeData>(
                passName,
                [&](rg::RenderGraph::Builder& builder, GlobalDistanceFieldCompositeData& data)
                {
                    builder.markSideEffect();

                    data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(cameraProxy.getViewId());
                    data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
                    data.drawBindGroup = &scene.getDrawBindGroup();

                    data.objectTable = builder.accessBuffer(objectTableHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.gdfIndirection = builder.accessBuffer(gdfIndirectionHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.meshInstanceData = builder.accessBuffer(meshInstanceDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                    data.sourceAtlas = builder.sample(sourceAtlasHandle);
                    data.outputAtlas = builder.storage(outputAtlasHandle, rg::AccessType::Read_Write);
                    outputAtlasHandle = data.outputAtlas; // 建立跟下一个 pass(如果有)的依赖链
                    data.objectGrid = builder.accessBuffer(objectGridHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Write);
                    objectGridHandle = data.objectGrid;

                    data.constants.sourceAtlasBrickGridExtent = sourceBrickGridExtent.x;
                    data.constants.outputAtlasBrickGridExtent = outputBrickGridExtent.x;
                    data.constants.mergeStaticLayer = mergeStaticLayer ? 1 : 0;

                    // cascade 信息表固定 4 条, 按 cascadeIndex 直接索引——没被这一批用到
                    // 的 cascade 留 0, 不会被任何 header 记录引用, 无害
                    std::vector<uint32_t> cascadeInfoWords(kGlobalDistanceFieldCascadeInfoTableWordCount, 0u);
                    std::vector<uint32_t> combinedHeader;
                    std::vector<uint32_t> combinedPayload;

                    for (const PendingLayer& pending : layers)
                    {
                        const GlobalDistanceField::Cascade& cascade = *pending.cascade;
                        GlobalDistanceField::Layer& layer = *pending.layer;

                        const double cellSizeD = static_cast<double>(2.0f * cascade.halfExtent) / kGlobalDistanceFieldBricksPerAxis;
                        const double3 gridMinD = cascade.center - double3(cascade.halfExtent);
                        WorldCoord gridMinCoord;
                        gridMinCoord.fromDouble3(gridMinD);

                        const uint32_t infoBase = static_cast<uint32_t>(pending.cascadeIndex) * kGlobalDistanceFieldCascadeInfoWordCount;
                        cascadeInfoWords[infoBase + 0] = static_cast<uint32_t>(gridMinCoord.tile.x);
                        cascadeInfoWords[infoBase + 1] = static_cast<uint32_t>(gridMinCoord.tile.y);
                        cascadeInfoWords[infoBase + 2] = static_cast<uint32_t>(gridMinCoord.tile.z);
                        cascadeInfoWords[infoBase + 3] = std::bit_cast<uint32_t>(gridMinCoord.offset.x);
                        cascadeInfoWords[infoBase + 4] = std::bit_cast<uint32_t>(gridMinCoord.offset.y);
                        cascadeInfoWords[infoBase + 5] = std::bit_cast<uint32_t>(gridMinCoord.offset.z);
                        cascadeInfoWords[infoBase + 6] = std::bit_cast<uint32_t>(static_cast<float>(cellSizeD));
                        cascadeInfoWords[infoBase + 7] = mergeStaticLayer
                            ? GlobalDistanceField::getLayerIndirectionOffset(pending.cascadeIndex, false)
                            : 0u;

                        // 把这一层的 pendingHeaderWords(相对自己 payload 段的 6 字记录)
                        // 拼进合并后的 header: 前插 cascadeSlot, payloadOffset 加上目前
                        // 已经累计的合并 payload 大小(变成相对合并 payload 段的绝对偏移)
                        const uint32_t payloadBase = static_cast<uint32_t>(combinedPayload.size());
                        const uint32_t headerCount = static_cast<uint32_t>(layer.pendingHeaderWords.size()) / kGlobalDistanceFieldCompositeHeaderWordCount;
                        combinedHeader.reserve(combinedHeader.size() + static_cast<size_t>(headerCount) * kGlobalDistanceFieldCombinedHeaderWordCount);
                        for (uint32_t i = 0; i < headerCount; ++i)
                        {
                            const uint32_t base = i * kGlobalDistanceFieldCompositeHeaderWordCount;
                            combinedHeader.push_back(static_cast<uint32_t>(pending.cascadeIndex));
                            combinedHeader.push_back(layer.pendingHeaderWords[base + 0]); // brickIndex
                            combinedHeader.push_back(layer.pendingHeaderWords[base + 1]); // candidateCount
                            combinedHeader.push_back(layer.pendingHeaderWords[base + 2] + payloadBase); // payloadOffset
                            combinedHeader.push_back(layer.pendingHeaderWords[base + 3]); // cellCoord.x
                            combinedHeader.push_back(layer.pendingHeaderWords[base + 4]); // cellCoord.y
                            combinedHeader.push_back(layer.pendingHeaderWords[base + 5]); // cellCoord.z
                        }
                        combinedPayload.insert(combinedPayload.end(), layer.pendingPayloadWords.begin(), layer.pendingPayloadWords.end());

                        // 这一层的合成工作在这一帧被这个 pass 认领了——数据已经拼进
                        // combinedHeader/combinedPayload, 不需要再留着等下一帧, 清空避免
                        // 这一层下次没脏时误用陈旧数据
                        layer.needsComposite = false;
                        layer.pendingHeaderWords.clear();
                        layer.pendingHeaderWords.shrink_to_fit();
                        layer.pendingPayloadWords.clear();
                        layer.pendingPayloadWords.shrink_to_fit();
                    }

                    data.totalBrickCount = static_cast<uint32_t>(combinedHeader.size() / kGlobalDistanceFieldCombinedHeaderWordCount);

                    // 回填 payloadOffset: 循环过程中每条记录里存的只是"相对合并后 payload
                    // 段自己起始位置"的偏移(payloadBase)——payload 段真正在 indices buffer 里的
                    // 绝对起始位置(cascade 信息表 + 整个合并 header 段)要等 combinedHeader
                    // 的最终大小确定之后才知道, 不能在上面的循环里边拼边算(那时候后面还有
                    // 别的层的 header 没拼进来, combinedHeader.size() 不是最终值)
                    const uint32_t payloadAbsoluteBase = kGlobalDistanceFieldCascadeInfoTableWordCount + static_cast<uint32_t>(combinedHeader.size());
                    for (uint32_t i = 0; i < combinedHeader.size(); i += kGlobalDistanceFieldCombinedHeaderWordCount)
                        combinedHeader[i + 3] += payloadAbsoluteBase;

                    data.indicesData.reserve(cascadeInfoWords.size() + combinedHeader.size() + combinedPayload.size());
                    data.indicesData.insert(data.indicesData.end(), cascadeInfoWords.begin(), cascadeInfoWords.end());
                    data.indicesData.insert(data.indicesData.end(), combinedHeader.begin(), combinedHeader.end());
                    data.indicesData.insert(data.indicesData.end(), combinedPayload.begin(), combinedPayload.end());

                    // GlobalBindings::mIndicesBuffer 是固定容量, 没有扩容机制——超出容量
                    // 就不写这一批, 直接跳过(这一批候选数据已经从 pendingHeaderWords 里
                    // 清掉了, 相当于这一帧的合成机会被放弃, 要等这些层下次被标脏才会重来;
                    // 正常场景规模下不应该触发, 触发了说明 IndicesBuffer::getCapacity()
                    // 需要调大)
                    if (data.indicesData.size() > IndicesBuffer::getCapacity())
                    {
                        // layer.needsComposite 在上面的循环里已经被清成 false 了(这批层已经
                        // 被"认领"), 但这里一丢, 真正的体素写入 dispatch 就不会发生——GDFIndirection
                        // 已经指向的新槽位这一帧不会被写入任何内容, 而 cascade.center 已经在
                        // 本函数前面重定心那一步推进到新值了, 两者从此不同步,
                        // 直到这些层哪天再被标脏重算才会自然修复
                        LOG_WARN("GlobalDistanceFieldCompositePass::addToRenderGraph: indicesData 超出 GlobalBindings 容量({} > {}), mergeStaticLayer={} 这一批 {} 个层的体素写入被丢弃, 但 needsComposite 已经清过了不会重试",
                            data.indicesData.size(), IndicesBuffer::getCapacity(),
                            mergeStaticLayer, layers.size());
                        data.indicesData.clear();
                        data.totalBrickCount = 0;
                    }
                },
                [&rendererContext](rg::ExecuteContext& context, GlobalDistanceFieldCompositeData& data)
                {
                    if (data.totalBrickCount == 0)
                        return;

                    auto& storage = rendererContext.getPassStorage().getOrCreate<GlobalDistanceFieldCompositePass, GlobalDistanceFieldCompositeStorage>();
                    auto& renderBackend = context.getRenderBackend();
                    auto& cmdList = context.getCommandList();

                    if (!storage.shader)
                    {
                        storage.shader = std::make_shared<PassShader>(
                            renderBackend.getDevice(),
                            "GlobalDistanceFieldComposite",
                            makeComputeBytecode(renderBackend.getRHIBackend(), GET_PASS_SHADER_BYTECODE(GlobalDistanceFieldComposite, Compute))
                        );
                    }

                    if (!storage.passLayout)
                    {
                        storage.passLayout = createPassBindGroupLayout(renderBackend, generated::kGlobalDistanceFieldComposite_Bindings);
                    }

                    if (!storage.pipeline)
                    {
                        storage.pipeline = createComputePipeline(rendererContext, storage.shader->getVariant(), *storage.passLayout, &rendererContext.getDrawBindGroupLayout());
                    }

                    if (!rendererContext.getIndicesBuffer().upload(cmdList, data.indicesData))
                        return;

                    rhi::Buffer* objectTableBuffer = context.getBuffer(data.objectTable);
                    rhi::Buffer* gdfIndirectionBuffer = context.getBuffer(data.gdfIndirection);
                    rhi::Buffer* meshInstanceDataBuffer = context.getBuffer(data.meshInstanceData);
                    rhi::Buffer* objectGridBuffer = context.getBuffer(data.objectGrid);
                    const rhi::TextureView* sourceAtlas = context.getTexture(data.sourceAtlas);
                    const rhi::TextureView* outputAtlas = context.getTexture(data.outputAtlas);
                    rhi::Sampler* linearSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                    cmdList.bindPipeline(storage.pipeline.get());
                    cmdList.setBindGroup(
                        1,
                        rhi::PipelineBindPoint::Compute,
                        &storage.pipeline->getLayout(),
                        data.viewBindGroup,
                        { &data.viewDataDynamicOffset, 1 }
                    );

                    auto& bindGroupCache = storage.bindGroupCaches[static_cast<size_t>(data.constants.mergeStaticLayer)];
                    if (bindGroupCache.needUpdate(objectTableBuffer, sourceAtlas, linearSampler, outputAtlas, gdfIndirectionBuffer, meshInstanceDataBuffer, objectGridBuffer))
                    {
                        bindGroupCache.update(
                            renderBackend,
                            *storage.passLayout,
                            [&](rhi::BindGroup& bindGroup)
                            {
                                PassBindGroupWriter writer(generated::kGlobalDistanceFieldComposite_Bindings);
                                writer.setBuffer("MeshShapeData", objectTableBuffer);
                                writer.setTexture("SourceBrickAtlas", sourceAtlas);
                                writer.setSampler("LinearSampler", linearSampler);
                                writer.setTexture("OutputBrickAtlas", outputAtlas);
                                writer.setBuffer("GDFIndirection", gdfIndirectionBuffer);
                                writer.setBuffer("MeshInstanceDataBuffer", meshInstanceDataBuffer);
                                writer.setBuffer("ObjectGrid", objectGridBuffer);
                                writer.apply(bindGroup);
                            }
                        );
                    }

                    cmdList.setBindGroup(
                        2,
                        rhi::PipelineBindPoint::Compute,
                        &storage.pipeline->getLayout(),
                        bindGroupCache.getBindGroup(),
                        {}
                    );
                    cmdList.setBindGroup(3, rhi::PipelineBindPoint::Compute, &storage.pipeline->getLayout(), data.drawBindGroup, {});
                    cmdList.setImmediateData(
                        &storage.pipeline->getLayout(),
                        0,
                        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&data.constants), sizeof(data.constants))
                    );
                    cmdList.dispatch(data.totalBrickCount, 1, 1);
                }
            );
        };

        addCompositePass("GlobalDistanceFieldCompositeStatic"_hs, false, pendingStaticLayers);
        addCompositePass("GlobalDistanceFieldCompositeDynamic"_hs, true, pendingDynamicLayers);

        // 存回去的是链式更新过的最终版本(如果两个 pass 都跑了, 就是动态层那次 builder.storage()
        // 返回的版本), 不是最初 import 时那份——GlobalDistanceFieldVisualizerPass 从 blackboard
        // 里查到的必须是"合成已经写完之后"这个版本, 它对这张纹理的 builder.sample() 才会被
        // render graph 正确识别成读后写依赖、自动插入 barrier, 而不是被当成一次独立的、跟这里
        // 的写入毫无关系的首次访问
        renderGraph.getBlackboard().put(outputAtlasKey, outputAtlasHandle);
        renderGraph.getBlackboard().put(objectGridKey, objectGridHandle);
    }
}
