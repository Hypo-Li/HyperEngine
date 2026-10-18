#include "MeshSDFPass.h"
#include "RenderPassUtils.h"

#include <RenderBase/MeshShapeProxy.h>
#include <RenderBase/DistanceFieldBrickAtlas.h>
#include "../SurfaceCacheAtlas.h"
#include "../LumenScene.h"

#include <MeshSDFScene.generated.h>

namespace hyp
{
    namespace
    {
        // 跟 Shader/MeshSDF/MeshSDFScene.slang 里的 MeshSDFPushConstants 逐字段对应,
        // 两边必须保持一致——引擎的 push constant 硬上限是 64 字节(rhi::kImmediateDataSize,
        // 见 Vulkan/Pipeline.cpp), 这里手动按 shader 那边的 HLSL/Slang cbuffer 打包规则
        // (float3/int3 紧跟一个标量拼进同一个 16 字节槽位)逐字段摆放, 不用 float4/int4
        // 图省事。brickGridResolution 不传, shader 端从 resolution 现算。resolution 后面
        // 原来跟了一个纯填充用的 pad0(凑满 16 字节槽位), 接 surface cache 采样时刚好用上:
        // cardCount 是这个 mesh 的卡片总数(采样时遍历 CardLookup 位图用)。
        // atlasBrickGridExtentX/Y/Z 合并成 1 个 int32_t: brick atlas 强制要求是立方体
        // (DistanceFieldBrickAtlas::chooseBrickGridExtent() 三个分量恒等), 分开存三份纯属
        // 浪费。cardAtlasPageGridExtentX 整个删掉, 页网格由 shader 端编译期常量
        // kSurfaceCacheAtlasResolution/kSurfaceCachePageSize 推出, 不需要当运行时值传。cardRangeSlotBase 是新增字段: 这个实例的 card
        // ranges 记录在 SurfaceCacheAtlas 场景级共享表(见 SurfaceCacheAtlas::
        // allocateCardPageTableSlots)里的起始下标, 数据合并成共享 buffer 之后不能再假设
        // "从第 0 个位置开始", 必须显式传。indirectionTableOffset 同理: 这个网格自己的
        // 间接索引表在 RenderBackend::getMeshShapeDataTable() 共享表里的起始
        // 下标(见 MeshShapeProxy::getIndirectionTableOffset())。目前总大小 60 字节,
        // 比 64 字节的硬上限还有 4 字节余量。
        struct MeshSDFPushConstants
        {
            float boundsMinX, boundsMinY, boundsMinZ;
            float narrowBandDistance;
            float boundsMaxX, boundsMaxY, boundsMaxZ;
            uint32_t instanceIndex;
            int32_t resolutionX, resolutionY, resolutionZ;
            int32_t cardCount;
            int32_t atlasBrickGridExtent; // 强制立方体, 只存 1 个分量
            uint32_t cardRangeSlotBase;
            uint32_t indirectionTableOffset;
        };
        static_assert(sizeof(MeshSDFPushConstants) == 60);
    }

    struct MeshSDFPrepareData
    {
    };

    struct MeshSDFData
    {
        uint32_t viewId;
        float4 viewport;
        const rhi::BindGroup* viewBindGroup;
        const rhi::BindGroup* instanceDataBindGroup;
        uint32_t viewDataDynamicOffset;
        MeshSDFPushConstants constants;
        rg::TextureHandle sceneColor;
        rg::TextureHandle brickAtlas;
        rg::BufferHandle objectTable;
        rg::BufferHandle cardPages;
        rg::TextureHandle cardAlbedoAtlas;
        rg::TextureHandle cardDepthAtlas;
    };

    struct MeshSDFStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<6>> perViewCaches;
    };

    namespace
    {
        // 没有可画的 SDF 实例、或者数据还没上传完时公用的兜底: 只 clear SceneColor, 不做
        // fullscreen 采样这一步——不能读一张还没真正传完数据的纹理(见 addToRenderGraph 里
        // 对 DistanceFieldBrickAtlas::isInitialized()/MeshShapeDataTable::hasPendingUploads()
        // 的检查, 那两个函数的注释解释了为什么不能想当然认为"这一帧 prepare() 过了就一定好了")
        void addClearOnlyPass(rg::RenderGraph& renderGraph, const SceneView& view)
        {
            const uint32_t viewId = view.getViewId();
            renderGraph.addGraphicsPass<MeshSDFData>(
                "MeshSDFClearOnly"_hs,
                [&](rg::RenderGraph::Builder& builder, MeshSDFData& data)
                {
                    data.viewport = view.getViewport();

                    rhi::TextureDesc sceneColorDesc;
                    sceneColorDesc.type = rhi::TextureType::Texture_2D;
                    sceneColorDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                    sceneColorDesc.extent = { uint32_t(data.viewport.z), uint32_t(data.viewport.w), 1 };
                    sceneColorDesc.mipCount = 1;
                    sceneColorDesc.layerCount = 1;
                    sceneColorDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                    auto sceneColor = builder.createTexture("SceneColor"_hs(viewId), sceneColorDesc);
                    data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Clear);
                    builder.getBlackboard().put("SceneColor"_hs(viewId), data.sceneColor);
                },
                [](rg::ExecuteContext&, MeshSDFData&) {}
            );
        }

        rhi::Pipeline* createMeshSDFPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        )
        {
            auto* pipelineLayout = createRenderPassPipelineLayout(rendererContext, &bindGroupLayout, nullptr, &rendererContext.getInstanceDataBindGroupLayout());
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

    void MeshSDFPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view
    )
    {
        const uint32_t viewId = view.getViewId();

        MeshRenderProxy* targetProxy = nullptr;
        for (MeshRenderProxy* meshRenderProxy : scene.getMeshRenderProxies())
        {
            MeshShapeProxy* meshShapeProxy = meshRenderProxy->getMeshShapeProxy();
            if (meshShapeProxy && meshShapeProxy->hasDistanceFieldData())
            {
                targetProxy = meshRenderProxy;
                break;
            }
        }

        if (!targetProxy)
        {
            // 场景里没有能画的距离场实例——仍然要把 SceneColor clear 一下, 不然这一路会读到
            // 从没被写过的未初始化纹理; 没有目标就没有 fullscreen 采样这一步
            addClearOnlyPass(renderGraph, view);
            return;
        }

        MeshShapeProxy* meshShapeProxy = targetProxy->getMeshShapeProxy();
        DistanceFieldBrickAtlas& brickAtlasPool = rendererContext.getRenderBackend().getMeshDistanceFieldBrickAtlas();
        MeshShapeDataTable& objectTable = rendererContext.getRenderBackend().getMeshShapeDataTable();

        // targetProxy 是按"有没有距离场数据"找出来的, 不代表它一定也有 Card 数据——这个场景
        // 可能压根没有任何实例烘焙过 Mesh Card, LumenScene 从来没被创建过(scene.getLumenScene()
        // 返回空), 下面凡是碰 SurfaceCacheAtlas 的地方都要先判空, 不能假设它存在
        LumenScene* lumenScene = scene.getLumenScene();

        // MeshShapeProxy/DistanceFieldBrickAtlas 是 Mesh 资产级别的 RenderBase 资源, 没有
        // 别的地方会替它做 flush, 只能自己在这里补上。不管这一帧最终画不画得成, 都要照常
        // 推进这个 flush(否则数据永远传不完)。SurfaceCacheAtlas 的 card 页表不需要在这里
        // 重复 flush——它的分配决策和 GPU 提交都收在 CardCapturePass 自己的 builder/execute
        // 里, 这个 pass 只读它的结果, 因此要求 CardCapturePass 必须声明在这个 pass 之前
        // (目前这个 pass 还没接入任何真实管线, 不构成现实约束, 但接入时必须保证这个顺序)。
        // MeshShapeProxy 自己不再持有任何待上传数据(metadata/间接索引表/Card 数据全部
        // 并进了共享的 MeshShapeDataTable), 这里只需要 flush 那张共享表和 brick atlas 池子
        renderGraph.addTransferPass<MeshSDFPrepareData>(
            "MeshSDFPrepare"_hs,
            [](rg::RenderGraph::Builder& builder, MeshSDFPrepareData&)
            {
                builder.markSideEffect();
            },
            [&rendererContext](rg::ExecuteContext& context, MeshSDFPrepareData&)
            {
                auto& cmdList = context.getCommandList();
                rendererContext.getRenderBackend().getMeshDistanceFieldBrickAtlas().prepare(cmdList);
                rendererContext.getRenderBackend().getMeshShapeDataTable().prepare(cmdList);
            }
        );

        // 上面 MeshSDFPrepare 这一帧才刚发出 flush 请求, 请求本身能不能在这一帧的 staging
        // 预算内实际完成是不确定的(prepare() 按整批分配, 分配失败就跳过等下一帧重试)。这里
        // 查的是"上一次已经跑完的 prepare() 结果"——只有它已经真正把数据传完、纹理布局已经
        // 转成 Shader_Sampled, 才能放心在下面把 DistanceFieldBrickAtlas 的 atlas 纹理 import
        // 成"当前处于 Shader_Sampled"——声明了但实际还是 None 会直接触发校验层报错。没准备好
        // 就退化成只 clear, 这一帧的 MeshSDFPrepare 仍然照常发出, 之后的某一帧自然会追上。
        // 没有 LumenScene、这个实例一张 card 都没有(比如 mesh 没跑过 bakeMeshCards)或者
        // ranges 表还没申请到(collectCardRequests 还没真正跑过一次成功分配)时也一并退化成
        // clear——没有 card 数据, surface cache 采样这条链路无从谈起, 不勉强画一个没有材质
        // 颜色的 SDF 出来。!lumenScene 排在 || 最前面短路求值, 后面几个条件才能放心解引用它
        if (!brickAtlasPool.isInitialized()
            || objectTable.hasPendingUploads()
            || !lumenScene
            || meshShapeProxy->getCardCount() == 0
            || !targetProxy->hasCardRanges())
        {
            addClearOnlyPass(renderGraph, view);
            return;
        }

        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();

        auto brickAtlasHandle = renderGraph.importTexture(
            "SparseBrickAtlas"_hs,
            &brickAtlasPool.getAtlasTextureView(),
            rhi::ResourceState::Shader_Sampled
        );
        // RenderBackend 持有的、跨网格共享的 Mesh 资产级别数据表(SDF metadata + 间接索引表
        // + Card 数据), 不再按实例/网格各自 import——每个网格只是这张共享 buffer 里的一段
        // 区间(见 MeshShapeProxy::getIndirectionTableOffset()/getCardLookupOffset()/
        // getCardDataOffset())
        auto objectTableHandle = renderGraph.importBuffer(
            "MeshShapeDataTable"_hs,
            objectTable.getBuffer()
        );
        // 现在是 SurfaceCacheAtlas 持有的场景级共享 buffer, 全场景一份, 不再按实例各建各的——
        // import 的 key 不用再按实例下标区分
        auto cardPagesHandle = renderGraph.importBuffer(
            "MeshSDFCardPages"_hs,
            surfaceCacheAtlas.getCardPageTableBuffer()
        );
        // SurfaceCacheAtlas 的两张图集是常驻跨帧资源, defaultState 语义(承诺/兑现)见
        // RenderGraph.h::Builder::importTexture 的注释——这两张纹理已经在 SurfaceCacheAtlas::
        // prepare() 里从 UNDEFINED 转过 Shader_Sampled 了, 这里放心声明
        auto cardAlbedoAtlasHandle = renderGraph.importTexture(
            "CardAtlasAlbedo"_hs,
            &surfaceCacheAtlas.getAlbedoAtlasTextureView(),
            rhi::ResourceState::Shader_Sampled
        );
        auto cardDepthAtlasHandle = renderGraph.importTexture(
            "CardAtlasDepth"_hs,
            &surfaceCacheAtlas.getDepthAtlasTextureView(),
            rhi::ResourceState::Shader_Sampled
        );

        renderGraph.addGraphicsPass<MeshSDFData>(
            "MeshSDFScene"_hs,
            [&](rg::RenderGraph::Builder& builder, MeshSDFData& data)
            {
                data.viewId = viewId;
                data.viewport = view.getViewport();
                data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);

                const int3 brickGridExtent = brickAtlasPool.getBrickGridExtent();
                assert(brickGridExtent.x == brickGridExtent.y && brickGridExtent.x == brickGridExtent.z
                    && "DistanceFieldBrickAtlas::chooseBrickGridExtent() 保证三个分量恒等(强制立方体), push constant 只传 1 个分量, 三个分量不一致说明这个前提被破坏了");
                const Boxf& bounds = meshShapeProxy->getDistanceFieldBounds();
                const float3 boundsMin = bounds.getMin();
                const float3 boundsMax = bounds.getMax();
                const int3 resolution = meshShapeProxy->getDistanceFieldResolution();

                MeshSDFPushConstants constants{};
                constants.boundsMinX = boundsMin.x;
                constants.boundsMinY = boundsMin.y;
                constants.boundsMinZ = boundsMin.z;
                constants.narrowBandDistance = meshShapeProxy->getDistanceFieldNarrowBandDistance();
                constants.boundsMaxX = boundsMax.x;
                constants.boundsMaxY = boundsMax.y;
                constants.boundsMaxZ = boundsMax.z;
                constants.instanceIndex = targetProxy->getInstanceIndex();
                constants.resolutionX = resolution.x;
                constants.resolutionY = resolution.y;
                constants.resolutionZ = resolution.z;
                constants.cardCount = static_cast<int32_t>(meshShapeProxy->getCardCount());
                constants.atlasBrickGridExtent = brickGridExtent.x;
                constants.cardRangeSlotBase = targetProxy->getCardRangeSlotBase();
                constants.indirectionTableOffset = meshShapeProxy->getIndirectionTableOffset();
                data.constants = constants;

                rhi::TextureDesc sceneColorDesc;
                sceneColorDesc.type = rhi::TextureType::Texture_2D;
                sceneColorDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                sceneColorDesc.extent = { uint32_t(data.viewport.z), uint32_t(data.viewport.w), 1 };
                sceneColorDesc.mipCount = 1;
                sceneColorDesc.layerCount = 1;
                sceneColorDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto sceneColor = builder.createTexture("SceneColor"_hs(viewId), sceneColorDesc);
                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Clear);
                builder.getBlackboard().put("SceneColor"_hs(viewId), data.sceneColor);

                data.brickAtlas = builder.sample(brickAtlasHandle);
                data.objectTable = builder.accessBuffer(
                    objectTableHandle,
                    rhi::BufferUsage::Storage,
                    rg::AccessType::Read_Only
                );
                data.cardPages = builder.accessBuffer(cardPagesHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.cardAlbedoAtlas = builder.sample(cardAlbedoAtlasHandle);
                data.cardDepthAtlas = builder.sample(cardDepthAtlasHandle);
            },
            [&rendererContext](rg::ExecuteContext& context, MeshSDFData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<MeshSDFPass, MeshSDFStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "MeshSDFScene",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(MeshSDFScene, Vertex),
                            GET_PASS_SHADER_BYTECODE(MeshSDFScene, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kMeshSDFScene_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                rhi::Buffer* objectTable = context.getBuffer(data.objectTable);
                const rhi::TextureView* brickAtlas = context.getTexture(data.brickAtlas);
                rhi::Buffer* cardPages = context.getBuffer(data.cardPages);
                const rhi::TextureView* cardAlbedoAtlas = context.getTexture(data.cardAlbedoAtlas);
                const rhi::TextureView* cardDepthAtlas = context.getTexture(data.cardDepthAtlas);
                rhi::Sampler* linearSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (bindGroupCache.needUpdate(objectTable, brickAtlas, cardPages, cardAlbedoAtlas, cardDepthAtlas, linearSampler))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kMeshSDFScene_Bindings);
                            writer.setBuffer("MeshShapeData", objectTable);
                            writer.setTexture("BrickAtlas", brickAtlas);
                            writer.setBuffer("CardPages", cardPages);
                            writer.setTexture("CardAlbedoAtlas", cardAlbedoAtlas);
                            writer.setTexture("CardDepthAtlas", cardDepthAtlas);
                            writer.setSampler("LinearSampler", linearSampler);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createMeshSDFPipeline(
                        rendererContext,
                        storage.shader->getVariant(),
                        bindGroupCache.getBindGroup()->getLayout()
                    );
                }

                setViewportAndScissor(cmdList, data.viewport);

                cmdList.setBindGroup(
                    1,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    data.viewBindGroup,
                    { &data.viewDataDynamicOffset, 1 }
                );

                cmdList.bindPipeline(storage.pipeline.get());
                cmdList.setBindGroup(
                    2,
                    rhi::PipelineBindPoint::Graphics,
                    &storage.pipeline->getLayout(),
                    bindGroupCache.getBindGroup(),
                    {}
                );
                cmdList.setBindGroup(3, rhi::PipelineBindPoint::Graphics, &storage.pipeline->getLayout(), data.instanceDataBindGroup, {});
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
