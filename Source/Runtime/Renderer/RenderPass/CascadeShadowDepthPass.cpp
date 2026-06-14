#include "CascadeShadowDepthPass.h"
#include "RenderPassUtils.h"

namespace hyp
{
    struct CascadeShadowDepthData
    {
        SceneRendererContext* rendererContext;
        const SceneProxy* scene;
        float depthBias;
        float slopeBias;
        std::vector<uint32_t> viewIds;
        std::vector<float4> viewports;
        std::vector<std::shared_ptr<DrawItemBucket>> shadowDepthBuckets;
        rg::TextureHandle shadowDepth;
    };

    struct DrawBatch
    {
        MaterialProxy* material;
        GeometryProxy* geometry;
        uint32_t sectionIndex;
        uint32_t instanceCount;
        uint32_t firstInstance;
    };

    void CascadeShadowDepthPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        SceneRendererContext& rendererContext,
        const SceneProxy& scene,
        std::span<const SceneView> views,
        std::span<const RenderData> renderData,
        const DirectionalLightProxy& directionalLight
    )
    {
        renderGraph.addGraphicsPass<CascadeShadowDepthData>(
            "CascadeShadowDepth"_hs,
            [&](rg::RenderGraph::Builder& builder, CascadeShadowDepthData& data)
            {
                builder.markSideEffect();

                data.rendererContext = &rendererContext;
                data.scene = &scene;

                data.depthBias = directionalLight.getDepthBias();
                data.slopeBias = directionalLight.getSlopeBias();

                uint32_t cascadeCount = static_cast<uint32_t>(views.size());
                data.viewIds.resize(cascadeCount);
                data.viewports.resize(cascadeCount);
                data.shadowDepthBuckets.resize(cascadeCount);
                for (uint32_t i = 0; i < cascadeCount; i++)
                {
                    data.viewIds[i] = views[i].getViewId();
                    data.viewports[i] = views[i].getViewport();
                    data.shadowDepthBuckets[i] = renderData[i].shadowDepthBucket;
                }

                uint32_t cascadeSize = uint32_t(data.viewports[0].w);
                rg::TextureHandle shadowDepth = builder.createTexture(
                    views[0].getFinalRTName(),
                    rg::TextureDescription{
                        VK_IMAGE_TYPE_2D,
                        VK_FORMAT_D16_UNORM,
                        VkExtent3D{ cascadeSize * cascadeCount, cascadeSize, 1},
                        1, 1, 1
                    }
                );
                data.shadowDepth = builder.addDepthAttachment(shadowDepth, VK_ATTACHMENT_LOAD_OP_CLEAR, true, rg::ClearDepthValue::One);

                rg::Blackboard& blackboard = builder.getBlackboard();
                blackboard.put(views[0].getFinalRTName(), data.shadowDepth);
            },
            [](rg::ExecuteContext& context, CascadeShadowDepthData& data)
            {
                HString renderTag = "Builtin_ShadowDepth"_hs;
                auto& cmd = context.getCommandBuffer();
                uint32_t cascadeCount = static_cast<uint32_t>(data.shadowDepthBuckets.size());

                std::vector<DrawBatch> drawBatches;
                std::vector<uint32_t> instanceIndices;
                uint32_t totalInstanceCount = 0;
                for (uint32_t i = 0; i < cascadeCount; ++i)
                {
                    totalInstanceCount += static_cast<uint32_t>(data.shadowDepthBuckets[i]->size());
                }
                drawBatches.reserve(totalInstanceCount);
                instanceIndices.reserve(totalInstanceCount);

                std::vector<uint32_t> drawBatchCount(cascadeCount, 0);

                for (uint32_t i = 0; i < cascadeCount; i++)
                {
                    MaterialProxy* prevMaterial = nullptr;
                    GeometryProxy* prevGeometry = nullptr;
                    uint32_t prevSectionIndex = ~0u;

                    for (const auto& sortedDrawItem : *data.shadowDepthBuckets[i])
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
                            drawBatchCount[i]++;
                            prevMaterial = drawItem.material;
                            prevGeometry = drawItem.geometry;
                            prevSectionIndex = drawItem.sectionIndex;
                        }

                        drawBatches.back().instanceCount++;
                        instanceIndices.push_back(drawItem.instanceIndex);
                    }
                }

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

                float shadowDepthParams[2] = { data.depthBias, data.slopeBias };
                cmd.pushConstants(
                    data.rendererContext->getGlobalPipelineLayout().getHandle(),
                    VK_SHADER_STAGE_ALL,
                    0,
                    sizeof(shadowDepthParams),
                    shadowDepthParams
                );

                context.beginRenderPass();
                auto& viewDataManager = data.rendererContext->getViewDataManager();
                uint32_t drawBatchOffset = 0;

                for (uint32_t i = 0; i < cascadeCount; i++)
                {
                    VkDescriptorSet descriptorSets[] = { data.scene->getSceneAndViewDescriptorSet().getHandle() };
                    uint32_t dynamicOffsets[] = { viewDataManager.getViewDataDynamicOffset(data.viewIds[i]) };
                    cmd.bindDescriptorSets(
                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                        data.rendererContext->getGlobalPipelineLayout().getHandle(),
                        1,
                        descriptorSets,
                        dynamicOffsets
                    );

                    setViewportAndScissor(cmd, data.viewports[i]);

                    vk::Pipeline* currentPipeline = nullptr;
                    MaterialProxy* prevMaterial = nullptr;
                    GeometryProxy* prevGeometry = nullptr;
                    for (uint32_t j = 0; j < drawBatchCount[i]; ++j)
                    {
                        auto& drawBatch = drawBatches[drawBatchOffset + j];

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
                            currentPipeline = pipeline;
                        }

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

                        if (drawBatch.geometry != prevGeometry)
                        {
                            drawBatch.geometry->bind(cmd);
                            prevGeometry = drawBatch.geometry;
                        }

                        drawBatch.geometry->draw(cmd, drawBatch.sectionIndex, drawBatch.instanceCount, drawBatch.firstInstance);
                    }

                    drawBatchOffset += drawBatchCount[i];
                }
            },
            rg::PassFlags::Skip_Begin_Render_Pass
        );
    }

    vk::Pipeline* CascadeShadowDepthPass::createPipeline(
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
        pipelineKey.pipelineState.depthCompareOp = static_cast<uint32_t>(CompareOp::Less);
        pipelineKey.pipelineState.stencilTestEnable = 0;
        pipelineKey.pipelineState.stencilState = StencilState::get();
        pipelineKey.pipelineState.colorBlendState = ColorBlendState::get();
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<VK_FORMAT_D16_UNORM>();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
