#include "GBufferPass.h"
#include "RenderPassUtils.h"

namespace hyp
{
    struct GBufferData
    {
        std::shared_ptr<DrawItemBucket> opaqueBucket;
        const SceneProxy* scene;
        float4 viewport;
        rg::TextureHandle sceneColor;
        rg::TextureHandle gbufferA;
        rg::TextureHandle gbufferB;
        rg::TextureHandle gbufferC;
        rg::TextureHandle sceneDepth;
    };

    struct DrawBatch
    {
        MaterialProxy* material;
        GeometryProxy* geometry;
        uint32_t sectionIndex;
        uint32_t instanceCount;
        uint32_t firstInstance;
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
                data.opaqueBucket = renderData.opaqueBucket;
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
                gbufferADesc.format = rhi::TextureFormat::A2BGR10_UNORM_PACK32;
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
                data.sceneDepth = builder.addDepthStencilAttachment(sceneDepth, rhi::AttachmentLoadOp::Clear, true, rhi::AttachmentLoadOp::Clear, false);

                rg::Blackboard& blackboard = builder.getBlackboard();
                blackboard.put("SceneColor"_hs(viewId), data.sceneColor);
                blackboard.put("GBufferA"_hs(viewId), data.gbufferA);
                blackboard.put("GBufferB"_hs(viewId), data.gbufferB);
                blackboard.put("GBufferC"_hs(viewId), data.gbufferC);
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
            },
            [&rendererContext](rg::ExecuteContext& context, GBufferData& data)
            {
                if (data.opaqueBucket->empty())
                {
                    context.beginPass();
                    return;
                }

                HString renderTag = "Builtin_Opaque"_hs;
                auto& cmd = context.getCommandList();

                std::vector<DrawBatch> drawBatches;
                std::vector<uint32_t> instanceIndices;
                drawBatches.reserve(data.opaqueBucket->size());
                instanceIndices.reserve(data.opaqueBucket->size());

                MaterialProxy* prevMaterial = nullptr;
                GeometryProxy* prevGeometry = nullptr;
                uint32_t prevSectionIndex = ~0u;

                for (const auto& sortedDrawItem : *data.opaqueBucket)
                {
                    auto& drawItem = sortedDrawItem.drawItem;

                    bool canInstance = (drawItem.material == prevMaterial) &&
                        (drawItem.geometry == prevGeometry) &&
                        (drawItem.sectionIndex == prevSectionIndex);
                    if (!canInstance)
                    {
                        drawBatches.emplace_back(
                            drawItem.material,
                            drawItem.geometry,
                            drawItem.sectionIndex,
                            0,
                            static_cast<uint32_t>(instanceIndices.size())
                        );
                        prevMaterial = drawItem.material;
                        prevGeometry = drawItem.geometry;
                        prevSectionIndex = drawItem.sectionIndex;
                    }

                    drawBatches.back().instanceCount++;
                    instanceIndices.push_back(drawItem.instanceIndex);
                }
                if (drawBatches.empty()) return;

                auto& indicesBuffer = data.scene->getIndicesBuffer();

                size_t indicesBufferSize = instanceIndices.size() * sizeof(uint32_t);
                auto allocation = rendererContext.getRenderBackend().getStagingAllocator().forceAllocate(indicesBufferSize);
                std::memcpy(allocation.mappedData, instanceIndices.data(), indicesBufferSize);

                rhi::BufferBarrier preBarrier;
                preBarrier.buffer = &indicesBuffer;
                preBarrier.offset = 0;
                preBarrier.size = indicesBufferSize;
                preBarrier.oldState = rhi::ResourceState::Shader_Storage_Read;
                preBarrier.newState = rhi::ResourceState::Transfer_Dst;
                cmd.insertBarriers({ &preBarrier, 1 });

                rhi::BufferCopyRegion copyRegion;
                copyRegion.srcOffset = allocation.offset;
                copyRegion.dstOffset = 0;
                copyRegion.size = indicesBufferSize;
                cmd.copyBuffer(allocation.buffer, &indicesBuffer, { &copyRegion, 1 });

                rhi::BufferBarrier postBarrier;
                postBarrier.buffer = &indicesBuffer;
                postBarrier.offset = 0;
                postBarrier.size = indicesBufferSize;
                postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
                cmd.insertBarriers({ &postBarrier, 1 });

                context.beginPass();

                setViewportAndScissor(cmd, data.viewport);

                rhi::Pipeline* currentPipeline = nullptr;
                prevMaterial = nullptr;
                prevGeometry = nullptr;
                for (const auto& drawBatch : drawBatches)
                {
                    VertexLayout vertexLayout = drawBatch.geometry->getVertexLayout();
                    rhi::Pipeline* pipeline = drawBatch.material->getCachedPipeline(renderTag, vertexLayout);
                    if (!pipeline)
                    {
                        pipeline = createPipeline(rendererContext, *drawBatch.material, renderTag, vertexLayout);
                        drawBatch.material->cachePipeline(renderTag, vertexLayout, pipeline);
                    }
                    if (pipeline != currentPipeline)
                    {
                        cmd.bindPipeline(pipeline);
                        currentPipeline = pipeline;
                    }

                    if (drawBatch.material != prevMaterial)
                    {
                        const rhi::BindGroup* parametersBindGroup = drawBatch.material->getParametersBindGroup();
                        if (parametersBindGroup)
                        {
                            cmd.setBindGroup(
                                2,
                                rhi::PipelineBindPoint::Graphics,
                                &currentPipeline->getLayout(),
                                parametersBindGroup,
                                {}
                            );
                        }
                        prevMaterial = drawBatch.material;
                    }

                    if (drawBatch.geometry != prevGeometry)
                    {
                        drawBatch.geometry->bind(cmd);
                        prevGeometry = drawBatch.geometry;
                    }

                    drawBatch.geometry->draw(cmd, drawBatch.sectionIndex, drawBatch.instanceCount, drawBatch.firstInstance);
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

        const rhi::BindGroup* materialParameterBindGroup = material.getParametersBindGroup();
        auto* pipelineLayout = createRenderPassPipelineLayout(
            rendererContext,
            nullptr,
            materialParameterBindGroup ? &materialParameterBindGroup->getLayout() : nullptr
        );

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState = PipelineStatePresets::opaqueMesh(material.getAttributes().doubleSided, rhi::CompareOp::Greater_Equal);
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.vertexLayout = vertexLayout;
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            rhi::TextureFormat::D32_SFLOAT_S8_UINT,
            rhi::TextureFormat::RGBA16_SFLOAT,
            rhi::TextureFormat::A2BGR10_UNORM_PACK32,
            rhi::TextureFormat::RGBA8_UNORM,
            rhi::TextureFormat::RGBA8_SRGB
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

}
