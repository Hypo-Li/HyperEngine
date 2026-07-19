#include "CascadeShadowDepthPass.h"
#include "RenderPassUtils.h"

#include <vector>

namespace hyp
{
    struct CascadeShadowDepthData
    {
        const SceneProxy* scene;
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

    // ShadowDepth 这个 render tag 的 shader(见 Shader/Material/ShadowDepth.slang)只用得到
    // Position(必需)和 Normal(算坡度阴影偏移), Tangent/Color/TexCoord.. 这些和它完全无关——
    // 对走共享 shader 路径的静态网格, 把用来查 pipeline/shader 变体缓存的 vertexLayout 归一化成
    // "有没有法线"这一个维度, 能让原本因为这些无关属性不同而各自独立的 pipeline 收敛到最多两个。
    // 这个归一化只对这个 pass 自己有意义(比如以后的 preDepth 可能连 Normal 都不需要), 所以放在
    // 这里而不是 MaterialProxy/MaterialShaderManager 那类被多个 render tag 共用的通用机制里；
    // GeometryProxy::bind() 依然照绑 mesh 真实的全部 buffer, 多绑的部分被 pipeline 忽略是安全的
    // (binding 索引分配已经保证按同一套优先级顺序对齐, 见 VertexLayout::writeVertexLayoutBindingsAndAttributes)
    static VertexLayout getSharedShadowVertexLayout(VertexLayout meshVertexLayout)
    {
        VertexAttributeMask mask = VertexAttributeMask::Position;
        if (enumHasAnyFlags(meshVertexLayout.getAttributeMask(), VertexAttributeMask::Normal))
        {
            mask |= VertexAttributeMask::Normal;
        }
        return VertexLayout(VertexLayoutType::Static_Mesh, mask);
    }

    void CascadeShadowDepthPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
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

                data.scene = &scene;

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

                rhi::TextureDesc shadowDepthDesc;
                shadowDepthDesc.type = rhi::TextureType::Texture_2D;
                shadowDepthDesc.format = rhi::TextureFormat::D16_UNORM;
                shadowDepthDesc.extent = { cascadeSize * cascadeCount, cascadeSize, 1 };
                shadowDepthDesc.mipCount = 1;
                shadowDepthDesc.layerCount = 1;
                shadowDepthDesc.samples = 1;
                shadowDepthDesc.usage = rhi::TextureUsage::Depth_Stencil_Attachment | rhi::TextureUsage::Sampled;
                rg::TextureHandle shadowDepth = builder.createTexture(views[0].getFinalRTName(), shadowDepthDesc);

                data.shadowDepth = builder.addDepthAttachment(shadowDepth, rhi::AttachmentLoadOp::Clear, true, 1.0f);

                rg::Blackboard& blackboard = builder.getBlackboard();
                blackboard.put(views[0].getFinalRTName(), data.shadowDepth);
            },
            [&rendererContext](rg::ExecuteContext& context, CascadeShadowDepthData& data)
            {
                HString renderTag = "Builtin_ShadowDepth"_hs;
                auto& cmdList = context.getCommandList();
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

                auto& stagingAllocator = rendererContext.getRenderBackend().getStagingAllocator();

                auto& indicesBuffer = data.scene->getIndicesBuffer();
                size_t indicesBufferSize = instanceIndices.size() * sizeof(uint32_t);
                auto indicesAllocation = stagingAllocator.forceAllocate(indicesBufferSize);
                std::memcpy(indicesAllocation.mappedData, instanceIndices.data(), indicesBufferSize);

                rhi::BufferBarrier preBarrier;
                preBarrier.buffer = &indicesBuffer;
                preBarrier.offset = 0;
                preBarrier.size = indicesBufferSize;
                preBarrier.oldState = rhi::ResourceState::Shader_Storage_Read;
                preBarrier.newState = rhi::ResourceState::Transfer_Dst;
                cmdList.insertBarriers({ &preBarrier, 1 });

                {
                    rhi::BufferCopyRegion copyRegion;
                    copyRegion.srcOffset = indicesAllocation.offset;
                    copyRegion.dstOffset = 0;
                    copyRegion.size = indicesBufferSize;
                    cmdList.copyBuffer(indicesAllocation.buffer, &indicesBuffer, { &copyRegion, 1 });
                }

                rhi::BufferBarrier postBarrier;
                postBarrier.buffer = &indicesBuffer;
                postBarrier.offset = 0;
                postBarrier.size = indicesBufferSize;
                postBarrier.oldState = rhi::ResourceState::Transfer_Dst;
                postBarrier.newState = rhi::ResourceState::Shader_Storage_Read;
                cmdList.insertBarriers({ &postBarrier, 1 });

                context.beginPass();
                auto& viewDataManager = rendererContext.getViewDataManager();
                uint32_t drawBatchOffset = 0;

                for (uint32_t i = 0; i < cascadeCount; i++)
                {
                    const rhi::BindGroup& sceneAndViewBindGroup = data.scene->getSceneAndViewBindGroup();
                    uint32_t dynamicOffset = viewDataManager.getViewDataDynamicOffset(data.viewIds[i]);
                    cmdList.setBindGroup(
                        1,
                        rhi::PipelineBindPoint::Graphics,
                        &rendererContext.getGlobalPipelineLayout(),
                        &sceneAndViewBindGroup,
                        { &dynamicOffset, 1 }
                    );

                    setViewportAndScissor(cmdList, data.viewports[i]);

                    rhi::Pipeline* currentPipeline = nullptr;
                    MaterialProxy* prevMaterial = nullptr;
                    GeometryProxy* prevGeometry = nullptr;
                    for (uint32_t j = 0; j < drawBatchCount[i]; ++j)
                    {
                        auto& drawBatch = drawBatches[drawBatchOffset + j];

                        VertexLayout vertexLayout = drawBatch.geometry->getVertexLayout();
                        // 只对"真的会走共享 shader 路径、且是静态网格"的组合做归一化——非共享路径
                        // (有顶点位置偏移/Masked 等)、或骨骼网格(现在 writeVertexLayoutBindingsAndAttributes
                        // 还没实现 Skeletal_Mesh 分支, 归一化到 Static_Mesh 类型没有意义), 都保持用
                        // 网格自己真实的 vertexLayout, 行为和归一化之前一致
                        if (drawBatch.material->usesSharedGeometryShader(renderTag) &&
                            vertexLayout.getType() == VertexLayoutType::Static_Mesh)
                        {
                            vertexLayout = getSharedShadowVertexLayout(vertexLayout);
                        }
                        rhi::Pipeline* pipeline = drawBatch.material->getCachedPipeline(renderTag, vertexLayout);
                        if (!pipeline)
                        {
                            pipeline = createPipeline(rendererContext, *drawBatch.material, renderTag, vertexLayout);
                            drawBatch.material->cachePipeline(renderTag, vertexLayout, pipeline);
                        }
                        if (pipeline != currentPipeline)
                        {
                            cmdList.bindPipeline(pipeline);
                            currentPipeline = pipeline;
                        }

                        if (drawBatch.material != prevMaterial)
                        {
                            // 共享 shader 的 pipeline layout 根本没有材质参数这个 set,即便材质
                            // 自己的 mParametersBindGroup 因为要给 Gbuffer 用而非空,这里也不能绑
                            const rhi::BindGroup* parametersBindGroup = drawBatch.material->usesSharedGeometryShader(renderTag)
                                ? nullptr
                                : drawBatch.material->getParametersBindGroup();
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
                            prevMaterial = drawBatch.material;
                        }

                        if (drawBatch.geometry != prevGeometry)
                        {
                            drawBatch.geometry->bind(cmdList);
                            prevGeometry = drawBatch.geometry;
                        }

                        drawBatch.geometry->draw(cmdList, drawBatch.sectionIndex, drawBatch.instanceCount, drawBatch.firstInstance);
                    }

                    drawBatchOffset += drawBatchCount[i];
                }
            },
            rg::PassFlags::Skip_Begin_Pass
        );
    }

    rhi::Pipeline* CascadeShadowDepthPass::createPipeline(
        RendererContext& rendererContext,
        MaterialProxy& material,
        HString renderTag,
        VertexLayout vertexLayout
    )
    {
        auto shaderVariant = material.getOrCreateShaderVariant(renderTag, vertexLayout);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        const bool usesSharedGeometryShader = material.usesSharedGeometryShader(renderTag);
        const rhi::BindGroup* materialParameterBindGroup = usesSharedGeometryShader ? nullptr : material.getParametersBindGroup();
        auto* pipelineLayout = createRenderPassPipelineLayout(
            rendererContext,
            nullptr,
            materialParameterBindGroup ? &materialParameterBindGroup->getLayout() : nullptr
        );

        PipelineCache::GraphicsKey pipelineKey;
        // 这里用正向 Z 的 Less, 不是其它 pass 统一使用的 reversed-Z Greater_Equal——阴影贴图独立
        // 于主相机深度约定, 沿用的是传统正向深度比较
        pipelineKey.pipelineState = PipelineStatePresets::opaqueMesh(material.getAttributes().doubleSided, rhi::CompareOp::Less);
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.vertexLayout = vertexLayout;
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::D16_UNORM>();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
