#include "GBufferPass.h"
#include "RenderPassUtils.h"

namespace hyp
{
    struct GBufferData
    {
        std::shared_ptr<DrawItemBucket> opaqueBucket;
        SceneRendererContext* rendererContext;
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
        SceneRendererContext& rendererContext,
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
                data.rendererContext = &rendererContext;
                data.scene = &scene;
                data.viewport = view.getViewport();

                uint32_t viewId = view.getViewId();
                uint32_t width = uint32_t(data.viewport.z);
                uint32_t height = uint32_t(data.viewport.w);

                auto sceneColor = builder.createTexture(
                    "SceneColor"_hs(viewId),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_R16G16B16A16_SFLOAT,
                        VkExtent3D{ width, height, 1 },
                        1, 1, 1
                    }
                );
                auto gbufferA = builder.createTexture(
                    "GBufferA"_hs(viewId),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_A2R10G10B10_UNORM_PACK32,
                        VkExtent3D{ width, height, 1 },
                        1, 1, 1
                    }
                );
                auto gbufferB = builder.createTexture(
                    "GBufferB"_hs(viewId),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_R8G8B8A8_UNORM,
                        VkExtent3D{ width, height, 1 },
                        1, 1, 1
                    }
                );
                auto gbufferC = builder.createTexture(
                    "GBufferC"_hs(viewId),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_R8G8B8A8_SRGB,
                        VkExtent3D{ width, height, 1 },
                        1, 1, 1
                    }
                );
                auto sceneDepth = builder.createTexture(
                    "SceneDepth"_hs(viewId),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_D32_SFLOAT_S8_UINT,
                        VkExtent3D{ width, height, 1 },
                        1, 1, 1
                    }
                );
                data.sceneColor = builder.addColorAttachment(sceneColor, VK_ATTACHMENT_LOAD_OP_CLEAR);
                data.gbufferA = builder.addColorAttachment(gbufferA, VK_ATTACHMENT_LOAD_OP_CLEAR);
                data.gbufferB = builder.addColorAttachment(gbufferB, VK_ATTACHMENT_LOAD_OP_CLEAR);
                data.gbufferC = builder.addColorAttachment(gbufferC, VK_ATTACHMENT_LOAD_OP_CLEAR);
                data.sceneDepth = builder.addDepthStencilAttachment(sceneDepth, VK_ATTACHMENT_LOAD_OP_CLEAR, true, VK_ATTACHMENT_LOAD_OP_DONT_CARE, false);
                rg::Blackboard& blackboard = builder.getBlackboard();
                blackboard.put("SceneColor"_hs(viewId), data.sceneColor);
                blackboard.put("GBufferA"_hs(viewId), data.gbufferA);
                blackboard.put("GBufferB"_hs(viewId), data.gbufferB);
                blackboard.put("GBufferC"_hs(viewId), data.gbufferC);
                blackboard.put("SceneDepth"_hs(viewId), data.sceneDepth);

                rg::TextureHandle sceneDepthD = builder.createSubTexture(
                    "SceneDepthD"_hs(viewId),
                    data.sceneDepth,
                    rg::SubTextureDescription{
                        VK_IMAGE_ASPECT_DEPTH_BIT,
                        0, 1, 0, 1
                    }
                );
                rg::TextureHandle sceneDepthS = builder.createSubTexture(
                    "SceneDepthS"_hs(viewId),
                    data.sceneDepth,
                    rg::SubTextureDescription{
                        VK_IMAGE_ASPECT_STENCIL_BIT,
                        0, 1, 0, 1
                    }
                );
                blackboard.put("SceneDepthD"_hs(viewId), sceneDepthD);
                blackboard.put("SceneDepthS"_hs(viewId), sceneDepthS);
            },
            [](rg::ExecuteContext& context, GBufferData& data)
            {
                if (data.opaqueBucket->empty())
                {
                    context.beginRenderPass();
                    return;
                }

                HString renderTag = "Builtin_Opaque"_hs;
                auto& cmd = context.getCommandBuffer();

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

                auto& instanceIndicesBuffer = data.scene->getInstanceIndicesBuffer();

                size_t indicesBufferSize = instanceIndices.size() * sizeof(uint32_t);
                if (indicesBufferSize <= 4096)
                {
                    // 小于等于 4KB (1024 个索引), 走 UpdateBuffer
                    cmd.updateBuffer(
                        instanceIndicesBuffer.getHandle(),
                        0,
                        indicesBufferSize,
                        instanceIndices.data()
                    );
                }
                else
                {
                    // 大于 4KB, 走 CopyBuffer
                    StagingAllocation allocation = data.rendererContext->getRenderBackend().getStagingAllocator().forceAllocate(indicesBufferSize);
                    std::memcpy(allocation.mappedData, instanceIndices.data(), indicesBufferSize);
                    VkBufferCopy copyRegion = {};
                    copyRegion.srcOffset = allocation.offset;
                    copyRegion.dstOffset = 0;
                    copyRegion.size = indicesBufferSize;
                    cmd.copyBuffer(
                        allocation.buffer,
                        instanceIndicesBuffer.getHandle(),
                        std::span(&copyRegion, 1)
                    );
                }

                VkBufferMemoryBarrier2 barrier = {};
                barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                barrier.buffer = instanceIndicesBuffer.getHandle();
                barrier.offset = 0;
                barrier.size = indicesBufferSize;
                cmd.pipelineBarrier2({ &barrier, 1 });

                setViewportAndScissor(cmd, data.viewport);

                context.beginRenderPass();

                vk::Pipeline* currentPipeline = nullptr;
                prevMaterial = nullptr;
                prevGeometry = nullptr;
                for (const auto& drawBatch : drawBatches)
                {
                    // bind pipeline
                    VertexLayout vertexLayout = drawBatch.geometry->getVertexLayout();
                    vk::Pipeline* pipeline = drawBatch.material->getCachedPipeline(renderTag, vertexLayout);
                    if (!pipeline)
                    {
                        pipeline = createPipeline(*data.rendererContext, *drawBatch.material, renderTag, vertexLayout);
                        drawBatch.material->cachePipeline(renderTag, vertexLayout, pipeline);
                    }
                    if (pipeline != currentPipeline)
                    {
                        cmd.bindPipeline(pipeline->getBindPoint(), pipeline->getHandle());
                        //cmd.setPolygonMode(VK_POLYGON_MODE_FILL);
                        currentPipeline = pipeline;
                    }

                    // bind descriptor set
                    if (drawBatch.material != prevMaterial)
                    {
                        const vk::DescriptorSet* parametersDescriptorSet = drawBatch.material->getParametersDescriptorSet();
                        if (parametersDescriptorSet)
                        {
                            VkDescriptorSet descriptorSetHandle = parametersDescriptorSet->getHandle();
                            cmd.bindDescriptorSets(
                                currentPipeline->getBindPoint(),
                                currentPipeline->getLayout().getHandle(),
                                2, // Material DescriptorSet Slot
                                { &descriptorSetHandle, 1 },
                                {}
                            );
                        }
                        prevMaterial = drawBatch.material;
                    }

                    // bind geometry
                    if (drawBatch.geometry != prevGeometry)
                    {
                        drawBatch.geometry->bind(cmd);
                        prevGeometry = drawBatch.geometry;
                    }

                    drawBatch.geometry->draw(cmd, drawBatch.sectionIndex, drawBatch.instanceCount, drawBatch.firstInstance);
                }
            },
            rg::PassFlags::Skip_Begin_Render_Pass
        );
    }

    vk::Pipeline* GBufferPass::createPipeline(
        SceneRendererContext& rendererContext,
        MaterialProxy& material,
        HString renderTag,
        VertexLayout vertexLayout
    )
    {
        auto shaderVariant = material.getOrCreateShaderVariant(renderTag, vertexLayout);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        const vk::DescriptorSet* materialParameterDescriptorSet = material.getParametersDescriptorSet();
        auto* pipelineLayout = createRenderPassPipelineLayout(
            rendererContext,
            VK_NULL_HANDLE,
            materialParameterDescriptorSet ? materialParameterDescriptorSet->getLayout().getHandle() : VK_NULL_HANDLE
        );

        PipelineCache::GraphicsKey pipelineKey;
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.vertexLayout = vertexLayout;
        pipelineKey.pipelineState.topology = static_cast<uint32_t>(Topology::Triangle_List);
        pipelineKey.pipelineState.samples = static_cast<uint32_t>(SampleCount::Count_1);
        pipelineKey.pipelineState.polygonMode = static_cast<uint32_t>(PolygonMode::Fill);
        pipelineKey.pipelineState.cullMode = static_cast<uint32_t>(material.getAttributes().doubleSided ? CullMode::None : CullMode::Back);
        pipelineKey.pipelineState.frontFace = static_cast<uint32_t>(FrontFace::Counter_Clockwise);
        pipelineKey.pipelineState.depthTestEnable = 1;
        pipelineKey.pipelineState.depthWriteEnable = 1;
        pipelineKey.pipelineState.depthCompareOp = static_cast<uint32_t>(CompareOp::Greater_Or_Equal);
        pipelineKey.pipelineState.stencilTestEnable = 0;
        pipelineKey.pipelineState.stencilState = StencilState::get();
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_FORMAT_A2R10G10B10_UNORM_PACK32,
            VK_FORMAT_R8G8B8A8_UNORM,
            VK_FORMAT_R8G8B8A8_SRGB
        >();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

}
