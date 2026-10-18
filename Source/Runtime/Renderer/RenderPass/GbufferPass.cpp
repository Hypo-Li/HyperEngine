#include "GbufferPass.h"
#include "RenderPassUtils.h"

namespace hyp
{
    struct GBufferData
    {
        std::shared_ptr<DrawItemList> drawItems;
        std::shared_ptr<DrawBatchList> opaqueBatches;
        const SceneProxy* scene;
        float4 viewport;
        rg::TextureHandle sceneColor;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle gbufferD;
        rg::TextureHandle velocity;
        rg::TextureHandle sceneDepth;
    };

    void GBufferPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& view,
        const RenderData& renderData
    )
    {
        renderGraph.addGraphicsPass<GBufferData>(
            "GBuffer"_hs,
            [&](rg::RenderGraph::Builder& builder, GBufferData& data)
            {
                data.drawItems = renderData.drawItems;
                data.opaqueBatches = renderData.opaqueBatches;
                data.scene = &scene;
                data.viewport = view.getViewport();

                uint32_t viewId = view.getViewId();
                uint32_t width = uint32_t(data.viewport.z);
                uint32_t height = uint32_t(data.viewport.w);

                rhi::TextureDesc sceneColorDesc;
                sceneColorDesc.type = rhi::TextureType::Texture_2D;
                sceneColorDesc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                sceneColorDesc.extent = { width, height, 1 };
                sceneColorDesc.mipCount = 1;
                sceneColorDesc.layerCount = 1;
                sceneColorDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled | rhi::TextureUsage::Transfer_Src;
                auto sceneColor = builder.createTexture("SceneColor"_hs(viewId), sceneColorDesc);

                rhi::TextureDesc gbufferADesc;
                gbufferADesc.type = rhi::TextureType::Texture_2D;
                gbufferADesc.format = rhi::TextureFormat::RG32_UINT;
                gbufferADesc.extent = { width, height, 1 };
                gbufferADesc.mipCount = 1;
                gbufferADesc.layerCount = 1;
                gbufferADesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto gbufferA = builder.createTexture("GBufferA"_hs(viewId), gbufferADesc);

                rhi::TextureDesc gbufferBDesc;
                gbufferBDesc.type = rhi::TextureType::Texture_2D;
                gbufferBDesc.format = rhi::TextureFormat::RGBA8_UNORM;
                gbufferBDesc.extent = { width, height, 1 };
                gbufferBDesc.mipCount = 1;
                gbufferBDesc.layerCount = 1;
                gbufferBDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto gbufferB = builder.createTexture("GBufferB"_hs(viewId), gbufferBDesc);

                rhi::TextureDesc gbufferCDesc;
                gbufferCDesc.type = rhi::TextureType::Texture_2D;
                gbufferCDesc.format = rhi::TextureFormat::RGBA8_SRGB;
                gbufferCDesc.extent = { width, height, 1 };
                gbufferCDesc.mipCount = 1;
                gbufferCDesc.layerCount = 1;
                gbufferCDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto gbufferC = builder.createTexture("GBufferC"_hs(viewId), gbufferCDesc);

                rhi::TextureDesc gbufferDDesc;
                gbufferDDesc.type = rhi::TextureType::Texture_2D;
                gbufferDDesc.format = rhi::TextureFormat::RGBA8_UNORM;
                gbufferDDesc.extent = { width, height, 1 };
                gbufferDDesc.mipCount = 1;
                gbufferDDesc.layerCount = 1;
                gbufferDDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto gbufferD = builder.createTexture("GBufferD"_hs(viewId), gbufferDDesc);

                rhi::TextureDesc velocityDesc;
                velocityDesc.type = rhi::TextureType::Texture_2D;
                velocityDesc.format = rhi::TextureFormat::RGBA16_UNORM;
                velocityDesc.extent = { width, height, 1 };
                velocityDesc.mipCount = 1;
                velocityDesc.layerCount = 1;
                velocityDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                auto velocity = builder.createTexture("Velocity"_hs(viewId), velocityDesc);

                rhi::TextureDesc sceneDepthDesc;
                sceneDepthDesc.type = rhi::TextureType::Texture_2D;
                sceneDepthDesc.format = rhi::TextureFormat::D32_SFLOAT_S8_UINT;
                sceneDepthDesc.extent = { width, height, 1 };
                sceneDepthDesc.mipCount = 1;
                sceneDepthDesc.layerCount = 1;
                sceneDepthDesc.usage = rhi::TextureUsage::Depth_Stencil_Attachment | rhi::TextureUsage::Sampled;
                auto sceneDepth = builder.createTexture("SceneDepth"_hs(viewId), sceneDepthDesc);

                data.sceneColor = builder.addColorAttachment(sceneColor, rhi::AttachmentLoadOp::Clear);
                data.gbufferA = builder.addColorAttachment(gbufferA, rhi::AttachmentLoadOp::Clear);
                data.gbufferB = builder.addColorAttachment(gbufferB, rhi::AttachmentLoadOp::Clear);
                data.gbufferC = builder.addColorAttachment(gbufferC, rhi::AttachmentLoadOp::Clear);
                data.gbufferD = builder.addColorAttachment(gbufferD, rhi::AttachmentLoadOp::Clear);
                data.velocity = builder.addColorAttachment(velocity, rhi::AttachmentLoadOp::Clear);
                data.sceneDepth = builder.addDepthStencilAttachment(sceneDepth, rhi::AttachmentLoadOp::Clear, true, rhi::AttachmentLoadOp::Clear, false);

                rg::Blackboard& blackboard = builder.getBlackboard();
                blackboard.put("SceneColor"_hs(viewId), data.sceneColor);
                blackboard.put("GBufferA"_hs(viewId), data.gbufferA);
                blackboard.put("GBufferB"_hs(viewId), data.gbufferB);
                blackboard.put("GBufferC"_hs(viewId), data.gbufferC);
                blackboard.put("GBufferD"_hs(viewId), data.gbufferD);
                blackboard.put("Velocity"_hs(viewId), data.velocity);
                blackboard.put("SceneDepth"_hs(viewId), data.sceneDepth);

                rhi::TextureViewDesc sceneDepthDViewDesc;
                sceneDepthDViewDesc.type = rhi::TextureViewType::Texture_View_2D;
                sceneDepthDViewDesc.format = rhi::TextureFormat::D32_SFLOAT_S8_UINT;
                sceneDepthDViewDesc.range.aspect = rhi::TextureAspect::Depth;
                sceneDepthDViewDesc.range.baseMip = 0;
                sceneDepthDViewDesc.range.mipCount = 1;
                sceneDepthDViewDesc.range.baseLayer = 0;
                sceneDepthDViewDesc.range.layerCount = 1;
                rg::TextureHandle sceneDepthD = builder.createSubTexture("SceneDepthD"_hs(viewId), data.sceneDepth, sceneDepthDViewDesc);

                blackboard.put("SceneDepthD"_hs(viewId), sceneDepthD);

                // TAAPass 用: 半透明物体在 TranslucentPass 里往 stencil 打了个标记位(见
                // TranslucentPass.cpp 的 kTAAStencilBit), resolve 时要单独读出来做特殊处理——同一份
                // SceneDepth 附件再开一个 aspect=Stencil 的子视图, 跟上面 SceneDepthD 一个套路
                rhi::TextureViewDesc sceneStencilViewDesc;
                sceneStencilViewDesc.type = rhi::TextureViewType::Texture_View_2D;
                sceneStencilViewDesc.format = rhi::TextureFormat::D32_SFLOAT_S8_UINT;
                sceneStencilViewDesc.range.aspect = rhi::TextureAspect::Stencil;
                sceneStencilViewDesc.range.baseMip = 0;
                sceneStencilViewDesc.range.mipCount = 1;
                sceneStencilViewDesc.range.baseLayer = 0;
                sceneStencilViewDesc.range.layerCount = 1;
                rg::TextureHandle sceneStencil = builder.createSubTexture("SceneStencil"_hs(viewId), data.sceneDepth, sceneStencilViewDesc);

                blackboard.put("SceneStencil"_hs(viewId), sceneStencil);
            },
            [&rendererContext](rg::ExecuteContext& context, GBufferData& data)
            {
                if (data.opaqueBatches->batches.empty())
                {
                    context.beginPass();
                    return;
                }

                HString renderTag = BuiltinRenderTags::Opaque;
                auto& cmdList = context.getCommandList();

                const std::vector<DrawBatch>& drawBatches = data.opaqueBatches->batches;
                const std::vector<uint32_t>& instanceIndices = data.opaqueBatches->instanceIndices;

                if (!rendererContext.getIndicesBuffer().upload(cmdList, instanceIndices))
                {
                    context.beginPass();
                    return;
                }

                context.beginPass();

                setViewportAndScissor(cmdList, data.viewport);

                rhi::Pipeline* currentPipeline = nullptr;
                const rhi::PipelineLayout* instanceBindLayout = nullptr;
                MaterialProxy* prevMaterial = nullptr;
                GeometryProxy* prevGeometry = nullptr;
                for (const auto& drawBatch : drawBatches)
                {
                    const DrawItem& batchDrawItem = (*data.drawItems)[drawBatch.itemIndex];
                    MaterialProxy* batchMaterial = batchDrawItem.material;
                    VertexLayout vertexLayout = batchDrawItem.getVertexLayout();
                    rhi::Pipeline* pipeline = batchMaterial->getCachedPipeline(renderTag, vertexLayout);
                    if (!pipeline)
                    {
                        pipeline = createPipeline(rendererContext, *batchMaterial, renderTag, vertexLayout);
                        batchMaterial->cachePipeline(renderTag, vertexLayout, pipeline);
                    }
                    if (pipeline != currentPipeline)
                    {
                        cmdList.bindPipeline(pipeline);
                        currentPipeline = pipeline;
                    }

                    if (batchMaterial != prevMaterial)
                    {
                        const rhi::BindGroup* parametersBindGroup = batchMaterial->getParametersBindGroup();
                        if (parametersBindGroup)
                        {
                            cmdList.setBindGroup(
                                2,
                                rhi::PipelineBindPoint::Graphics,
                                &currentPipeline->getLayout(),
                                parametersBindGroup,
                                {}
                            );
                        }
                        prevMaterial = batchMaterial;
                    }

                    // set3 必须排在 set2 之后绑, 否则 set2 换了布局会让它失效
                    if (&currentPipeline->getLayout() != instanceBindLayout)
                    {
                        const rhi::BindGroup& instanceBindGroup = data.scene->getDrawBindGroup(vertexLayout.getType());
                        cmdList.setBindGroup(3, rhi::PipelineBindPoint::Graphics, &currentPipeline->getLayout(), &instanceBindGroup, {});
                        instanceBindLayout = &currentPipeline->getLayout();
                    }

                    if (batchDrawItem.geometry != prevGeometry)
                    {
                        batchDrawItem.bind(cmdList);
                        prevGeometry = batchDrawItem.geometry;
                    }

                    batchDrawItem.draw(cmdList, drawBatch.instanceCount, drawBatch.firstInstance);
                }
            },
            rg::PassFlags::Skip_Begin_Pass
        );
    }

    rhi::Pipeline* GBufferPass::createPipeline(
        RendererContext& rendererContext,
        MaterialProxy& material,
        HString renderTag,
        VertexLayout vertexLayout
    )
    {
        auto shaderVariant = material.getOrCreateShaderVariant(renderTag, vertexLayout);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        const rhi::BindGroupLayout* instanceLayout = &rendererContext.getDrawBindGroupLayout(vertexLayout.getType());

        const rhi::BindGroup* materialParameterBindGroup = material.getParametersBindGroup();
        auto* pipelineLayout = createRenderPassPipelineLayout(
            rendererContext,
            nullptr,
            materialParameterBindGroup ? &materialParameterBindGroup->getLayout() : nullptr,
            instanceLayout
        );

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::opaqueMesh(material.getAttributes().doubleSided, rhi::CompareOp::Greater_Equal);
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.vertexLayout = vertexLayout;
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::D32_SFLOAT_S8_UINT,
            rhi::TextureFormat::RGBA16_SFLOAT,
            rhi::TextureFormat::RG32_UINT,
            rhi::TextureFormat::RGBA8_UNORM,
            rhi::TextureFormat::RGBA8_SRGB,
            rhi::TextureFormat::RGBA8_UNORM,
            rhi::TextureFormat::RGBA16_UNORM
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

}
