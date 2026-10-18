#include "GlobalDistanceFieldVisualizerPass.h"
#include "RenderPassUtils.h"
#include "LumenSceneUpdatePass.h"

#include <RenderBase/DistanceFieldBrickAtlas.h>
#include "../CameraProxy.h"
#include "../SurfaceCacheAtlas.h"
#include "../GlobalDistanceField.h"
#include "../LumenScene.h"

#include <GlobalDistanceFieldVisualizer.generated.h>

namespace hyp
{
    namespace
    {
        // 跟 Shader/GlobalDistanceField/GlobalDistanceFieldVisualizer.slang 里的
        // GlobalDistanceFieldVisualizerPushConstants 逐字段对应
        struct GlobalDistanceFieldVisualizerPushConstants
        {
            int32_t gdfAtlasBrickGridExtent;
            int32_t mdfAtlasBrickGridExtent;
            uint32_t visualizeMode;
            uint32_t useMeshDistanceField;
        };
        static_assert(sizeof(GlobalDistanceFieldVisualizerPushConstants) == 16);
    }

    struct GlobalDistanceFieldVisualizerData
    {
        uint32_t viewId;
        float4 viewport;
        const rhi::BindGroup* viewBindGroup;
        const rhi::BindGroup* instanceDataBindGroup;
        uint32_t viewDataDynamicOffset;
        GlobalDistanceFieldVisualizerPushConstants constants;
        rg::TextureHandle sceneColor;
        rg::BufferHandle gdfIndirection;
        rg::BufferHandle gdfObjectGrid;
        rg::TextureHandle gdfAtlas;
        rg::BufferHandle objectTable;
        rg::BufferHandle meshInstanceData;
        rg::BufferHandle cardPages;
        rg::TextureHandle cardFinalLightingAtlas;
        rg::TextureHandle cardIndirectLightingAtlas;
        rg::TextureHandle cardDirectLightingAtlas;
        rg::TextureHandle cardAlbedoAtlas;
        rg::TextureHandle cardNormalAtlas;
        rg::TextureHandle cardEmissiveAtlas;
        rg::TextureHandle cardDepthAtlas;
        // per-mesh MDF 源数据(RenderBackend::getMeshDistanceFieldBrickAtlas())——只有
        // scene.getUseMeshDistanceField() 为真时精确求交才会用到, 但不管开关状态如何都无
        // 条件绑定(跟其它图集一致的简化, 见 addToRenderGraph 里的导入/守卫), shader 端
        // 自己按 PC.useMeshDistanceField 决定采不采样
        rg::TextureHandle mdfBrickAtlas;
    };

    struct GlobalDistanceFieldVisualizerStorage
    {
        std::shared_ptr<PassShader> shader;
        TRef<rhi::BindGroupLayout> bindGroupLayout;
        TRef<rhi::Pipeline> pipeline;
        std::vector<BindGroupCache<15>> perViewCaches;
    };

    namespace
    {
        // 没有可画的数据时公用的兜底: 只 clear SceneColor, 理由跟 MeshSDFPass::addClearOnlyPass
        // 一致——不能让下游 Pass 读到一张从没被写过的未初始化纹理
        void addClearOnlyPass(rg::RenderGraph& renderGraph, const SceneView& view)
        {
            const uint32_t viewId = view.getViewId();
            renderGraph.addGraphicsPass<GlobalDistanceFieldVisualizerData>(
                "GlobalDistanceFieldVisualizerClearOnly"_hs,
                [&](rg::RenderGraph::Builder& builder, GlobalDistanceFieldVisualizerData& data)
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
                [](rg::ExecuteContext&, GlobalDistanceFieldVisualizerData&) {}
            );
        }

        rhi::Pipeline* createGlobalDistanceFieldVisualizerPipeline(
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

    void GlobalDistanceFieldVisualizerPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const CameraProxy& camera)
    {
        const uint32_t viewId = view.getViewId();

        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
        {
            addClearOnlyPass(renderGraph, view);
            return;
        }

        RenderBackend& renderBackend = rendererContext.getRenderBackend();
        GlobalDistanceField& gdf = camera.getGlobalDistanceField();

        DistanceFieldBrickAtlas& gdfAtlasPool = lumenScene->getGlobalDistanceFieldBrickAtlas();
        // per-mesh MDF 源数据——只有 scene.getUseMeshDistanceField() 为真时精确求交才需要它
        // 就绪, 见下面的守卫和 GlobalDistanceFieldVisualizer.slang 里的 resolveMdfHit()
        DistanceFieldBrickAtlas& mdfAtlasPool = renderBackend.getMeshDistanceFieldBrickAtlas();
        MeshShapeDataTable& objectTable = renderBackend.getMeshShapeDataTable();

        // 数据没就绪时退化成只 clear——不能读一张/一份还没真正传完数据的纹理/buffer
        const LumenSceneUpdateResult lumenUpdate = LumenSceneUpdatePass::addToRenderGraph(
            renderGraph, rendererContext, scene, camera, scene.getUseMeshDistanceField());
        if (!lumenUpdate.ready)
        {
            addClearOnlyPass(renderGraph, view);
            return;
        }

        rhi::Buffer* gdfIndirectionBuffer = gdf.getIndirectionBuffer();
        rhi::Buffer* gdfObjectGridBuffer = gdf.getObjectGridBuffer();
        rg::TextureHandle cardFinalLightingAtlasHandle = lumenUpdate.cardFinalLightingAtlas;
        rg::TextureHandle cardIndirectLightingAtlasHandle = lumenUpdate.cardIndirectLightingAtlas;
        rg::TextureHandle cardDirectLightingAtlasHandle = lumenUpdate.cardDirectLightingAtlas;
        rg::TextureHandle cardAlbedoAtlasHandle = lumenUpdate.cardAlbedoAtlas;
        rg::TextureHandle cardNormalAtlasHandle = lumenUpdate.cardNormalAtlas;
        rg::TextureHandle cardEmissiveAtlasHandle = lumenUpdate.cardEmissiveAtlas;
        rg::TextureHandle cardDepthAtlasHandle = lumenUpdate.cardDepthAtlas;
        rg::BufferHandle cardPagesHandle = lumenUpdate.cardPages;

        // 这几份资源在 GlobalDistanceFieldCompositePass 里也 import 过——
        // 通过 blackboard 复用同一份句柄, 不重新 import, 理由见 getOrImportResource() 的注释。
        // GlobalDistanceFieldAtlas/GlobalDistanceFieldObjectGrid 尤其重要: 必须拿到 Composite 写完之后存回去的那个版本,
        // 这里的读访问才会被 render graph 识别成读后写依赖、自动插入 barrier
        HString gdfIndirectionKey = "GlobalDistanceFieldIndirection"_hs(camera.getViewId());
        auto gdfIndirectionHandle = getOrImportResource<rg::BufferHandle>(renderGraph, gdfIndirectionKey, [&]
        {
            return renderGraph.importBuffer(gdfIndirectionKey, gdfIndirectionBuffer);
        });
        HString gdfObjectGridKey = "GlobalDistanceFieldObjectGrid"_hs(camera.getViewId());
        auto gdfObjectGridHandle = getOrImportResource<rg::BufferHandle>(renderGraph, gdfObjectGridKey, [&]
        {
            return renderGraph.importBuffer(gdfObjectGridKey, gdfObjectGridBuffer);
        });
        HString gdfAtlasKey = "GlobalDistanceFieldAtlas"_hs(camera.getViewId());
        auto gdfAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, gdfAtlasKey, [&]
        {
            return renderGraph.importTexture(
                gdfAtlasKey,
                &gdfAtlasPool.getAtlasTextureView(),
                rhi::ResourceState::Shader_Sampled
            );
        });
        auto objectTableHandle = getOrImportResource<rg::BufferHandle>(renderGraph, "MeshShapeDataTable"_hs, [&]
        {
            return renderGraph.importBuffer("MeshShapeDataTable"_hs, objectTable.getBuffer());
        });
        HString meshInstanceDataKey = "SceneMeshInstanceData"_hs(scene.getSceneId());
        auto meshInstanceDataHandle = getOrImportResource<rg::BufferHandle>(renderGraph, meshInstanceDataKey, [&]
        {
            return renderGraph.importBuffer(meshInstanceDataKey, &scene.getMeshInstanceDataBuffer());
        });
        // per-mesh MDF 源数据——跟 CardDirectLightingPass.cpp 同一个 key("SparseBrickAtlas"_hs),
        // 那个 Pass 可能已经在上面(主相机分支)导入过一次, 用 getOrImportResource 兜底复用。
        // 还没就绪(!isInitialized())时绑一张黑色默认纹理占位——这种情况只会发生在
        // scene.getUseMeshDistanceField() 为 false 的时候(为 true 时上面已经早退), shader
        // 端也不会真的采样它, 这里只是为了不对一张布局还是 UNDEFINED 的纹理撒谎声明
        // Shader_Sampled(理由跟 SurfaceCacheAtlas::ensureCreated() 的注释一致)
        auto mdfBrickAtlasHandle = getOrImportResource<rg::TextureHandle>(renderGraph, "SparseBrickAtlas"_hs, [&]
        {
            rhi::TextureView& view = mdfAtlasPool.isInitialized()
                ? mdfAtlasPool.getAtlasTextureView()
                : renderBackend.getDefaultResources().getBlackTexture3DView();
            return renderGraph.importTexture("SparseBrickAtlas"_hs, &view, rhi::ResourceState::Shader_Sampled);
        });

        renderGraph.addGraphicsPass<GlobalDistanceFieldVisualizerData>(
            "GlobalDistanceFieldVisualizer"_hs,
            [&](rg::RenderGraph::Builder& builder, GlobalDistanceFieldVisualizerData& data)
            {
                data.viewId = viewId;
                data.viewport = view.getViewport();
                data.viewBindGroup = &rendererContext.getViewDataManager().getBindGroup();
                data.instanceDataBindGroup = &scene.getInstanceDataBindGroup();
                data.viewDataDynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(viewId);

                const int3 gdfBrickGridExtent = gdfAtlasPool.getBrickGridExtent();
                data.constants.gdfAtlasBrickGridExtent = gdfBrickGridExtent.x;
                data.constants.mdfAtlasBrickGridExtent = mdfAtlasPool.getBrickGridExtent().x;
                data.constants.visualizeMode = static_cast<uint32_t>(scene.getSceneVisualizeMode());
                data.constants.useMeshDistanceField = scene.getUseMeshDistanceField() ? 1u : 0u;

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

                data.gdfIndirection = builder.accessBuffer(gdfIndirectionHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.gdfObjectGrid = builder.accessBuffer(gdfObjectGridHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.gdfAtlas = builder.sample(gdfAtlasHandle);
                data.objectTable = builder.accessBuffer(objectTableHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.meshInstanceData = builder.accessBuffer(meshInstanceDataHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.cardPages = builder.accessBuffer(cardPagesHandle, rhi::BufferUsage::Storage, rg::AccessType::Read_Only);
                data.cardFinalLightingAtlas = builder.sample(cardFinalLightingAtlasHandle);
                data.cardIndirectLightingAtlas = builder.sample(cardIndirectLightingAtlasHandle);
                data.cardDirectLightingAtlas = builder.sample(cardDirectLightingAtlasHandle);
                data.cardAlbedoAtlas = builder.sample(cardAlbedoAtlasHandle);
                data.cardNormalAtlas = builder.sample(cardNormalAtlasHandle);
                data.cardEmissiveAtlas = builder.sample(cardEmissiveAtlasHandle);
                data.cardDepthAtlas = builder.sample(cardDepthAtlasHandle);
                data.mdfBrickAtlas = builder.sample(mdfBrickAtlasHandle);
            },
            [&rendererContext](rg::ExecuteContext& context, GlobalDistanceFieldVisualizerData& data)
            {
                auto& storage = rendererContext.getPassStorage().getOrCreate<GlobalDistanceFieldVisualizerPass, GlobalDistanceFieldVisualizerStorage>();
                auto& renderBackend = context.getRenderBackend();
                auto& cmdList = context.getCommandList();

                if (!storage.shader)
                {
                    storage.shader = std::make_shared<PassShader>(
                        renderBackend.getDevice(),
                        "GlobalDistanceFieldVisualizer",
                        makeVertexFragmentBytecode(
                            renderBackend.getRHIBackend(),
                            GET_PASS_SHADER_BYTECODE(GlobalDistanceFieldVisualizer, Vertex),
                            GET_PASS_SHADER_BYTECODE(GlobalDistanceFieldVisualizer, Fragment)
                        )
                    );
                }

                if (!storage.bindGroupLayout)
                {
                    storage.bindGroupLayout = createPassBindGroupLayout(renderBackend, generated::kGlobalDistanceFieldVisualizer_Bindings);
                }

                if (storage.perViewCaches.size() <= data.viewId)
                {
                    storage.perViewCaches.resize(data.viewId + 1);
                }
                auto& bindGroupCache = storage.perViewCaches[data.viewId];

                rhi::Buffer* gdfIndirection = context.getBuffer(data.gdfIndirection);
                rhi::Buffer* gdfObjectGrid = context.getBuffer(data.gdfObjectGrid);
                const rhi::TextureView* gdfAtlas = context.getTexture(data.gdfAtlas);
                rhi::Buffer* objectTable = context.getBuffer(data.objectTable);
                rhi::Buffer* meshInstanceData = context.getBuffer(data.meshInstanceData);
                rhi::Buffer* cardPages = context.getBuffer(data.cardPages);
                const rhi::TextureView* cardFinalLightingAtlas = context.getTexture(data.cardFinalLightingAtlas);
                const rhi::TextureView* cardIndirectLightingAtlas = context.getTexture(data.cardIndirectLightingAtlas);
                const rhi::TextureView* cardDirectLightingAtlas = context.getTexture(data.cardDirectLightingAtlas);
                const rhi::TextureView* cardAlbedoAtlas = context.getTexture(data.cardAlbedoAtlas);
                const rhi::TextureView* cardNormalAtlas = context.getTexture(data.cardNormalAtlas);
                const rhi::TextureView* cardEmissiveAtlas = context.getTexture(data.cardEmissiveAtlas);
                const rhi::TextureView* cardDepthAtlas = context.getTexture(data.cardDepthAtlas);
                const rhi::TextureView* mdfBrickAtlas = context.getTexture(data.mdfBrickAtlas);
                rhi::Sampler* linearSampler = renderBackend.getSamplerCache().getLinearClampNoMip();

                if (bindGroupCache.needUpdate(gdfIndirection, gdfAtlas, linearSampler, objectTable, gdfObjectGrid, meshInstanceData, cardPages, cardFinalLightingAtlas, cardIndirectLightingAtlas, cardDirectLightingAtlas, cardAlbedoAtlas, cardNormalAtlas, cardEmissiveAtlas, cardDepthAtlas, mdfBrickAtlas))
                {
                    bindGroupCache.update(
                        renderBackend,
                        *storage.bindGroupLayout,
                        [&](rhi::BindGroup& bindGroup)
                        {
                            PassBindGroupWriter writer(generated::kGlobalDistanceFieldVisualizer_Bindings);
                            writer.setBuffer("GdfIndirection", gdfIndirection);
                            writer.setTexture("GdfAtlas", gdfAtlas);
                            writer.setSampler("LinearSampler", linearSampler);
                            writer.setBuffer("MeshShapeData", objectTable);
                            writer.setBuffer("ObjectGrid", gdfObjectGrid);
                            writer.setBuffer("MeshInstanceDataBuffer", meshInstanceData);
                            writer.setBuffer("CardPages", cardPages);
                            writer.setTexture("CardFinalLightingAtlas", cardFinalLightingAtlas);
                            writer.setTexture("CardIndirectLightingAtlas", cardIndirectLightingAtlas);
                            writer.setTexture("CardDirectLightingAtlas", cardDirectLightingAtlas);
                            writer.setTexture("CardAlbedoAtlas", cardAlbedoAtlas);
                            writer.setTexture("CardNormalAtlas", cardNormalAtlas);
                            writer.setTexture("CardEmissiveAtlas", cardEmissiveAtlas);
                            writer.setTexture("CardDepthAtlas", cardDepthAtlas);
                            writer.setTexture("MeshDistanceFieldBrickAtlas", mdfBrickAtlas);
                            writer.apply(bindGroup);
                        }
                    );
                }

                if (!storage.pipeline)
                {
                    storage.pipeline = createGlobalDistanceFieldVisualizerPipeline(
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
