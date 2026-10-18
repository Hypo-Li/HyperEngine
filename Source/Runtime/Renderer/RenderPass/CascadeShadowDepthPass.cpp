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
        std::vector<float2> biases; // .x = depthBias, .y = slopeDepthBias, 按 cascade 对齐
        std::vector<std::shared_ptr<DrawItemList>> drawItems;
        std::vector<std::shared_ptr<DrawBatchList>> shadowDepthBatches;
        rg::TextureHandle shadowDepth;
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
        const DirectionalLightProxy& directionalLight,
        const CameraProxy& camera
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
                data.biases.resize(cascadeCount);
                data.drawItems.resize(cascadeCount);
                data.shadowDepthBatches.resize(cascadeCount);
                std::span<const CascadeShadowData> cascadeShadowData = directionalLight.getCascadeShadowData(&camera);
                assert(cascadeShadowData.size() == cascadeCount);
                for (uint32_t i = 0; i < cascadeCount; i++)
                {
                    data.viewIds[i] = views[i].getViewId();
                    data.viewports[i] = views[i].getViewport();
                    data.biases[i] = float2(cascadeShadowData[i].depthBias, cascadeShadowData[i].slopeDepthBias);
                    data.drawItems[i] = renderData[i].drawItems;
                    data.shadowDepthBatches[i] = renderData[i].shadowDepthBatches;
                }

                uint32_t cascadeSize = uint32_t(data.viewports[0].w);
                uint32_t atlasColumnCount = std::min(cascadeCount, DirectionalLightProxy::CascadeAtlasColumnCount);
                uint32_t atlasRowCount = (cascadeCount + DirectionalLightProxy::CascadeAtlasColumnCount - 1u)
                    / DirectionalLightProxy::CascadeAtlasColumnCount;

                rhi::TextureDesc shadowDepthDesc;
                shadowDepthDesc.type = rhi::TextureType::Texture_2D;
                shadowDepthDesc.format = rhi::TextureFormat::D16_UNORM;
                shadowDepthDesc.extent = { cascadeSize * atlasColumnCount, cascadeSize * atlasRowCount, 1 };
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
                HString renderTag = BuiltinRenderTags::ShadowDepth;
                auto& cmdList = context.getCommandList();
                uint32_t cascadeCount = static_cast<uint32_t>(data.shadowDepthBatches.size());

                std::vector<uint32_t> instanceIndices;
                std::vector<uint32_t> cascadeFirstInstance(cascadeCount, 0);
                uint32_t totalInstanceCount = 0;
                for (uint32_t i = 0; i < cascadeCount; ++i)
                {
                    totalInstanceCount += static_cast<uint32_t>(data.shadowDepthBatches[i]->instanceIndices.size());
                }
                instanceIndices.reserve(totalInstanceCount);

                for (uint32_t i = 0; i < cascadeCount; ++i)
                {
                    const std::vector<uint32_t>& cascadeInstanceIndices = data.shadowDepthBatches[i]->instanceIndices;
                    cascadeFirstInstance[i] = static_cast<uint32_t>(instanceIndices.size());
                    instanceIndices.insert(instanceIndices.end(), cascadeInstanceIndices.begin(), cascadeInstanceIndices.end());
                }

                if (!rendererContext.getIndicesBuffer().upload(cmdList, instanceIndices))
                {
                    context.beginPass();
                    return;
                }

                context.beginPass();
                auto& viewDataManager = rendererContext.getViewDataManager();

                for (uint32_t i = 0; i < cascadeCount; i++)
                {
                    uint32_t dynamicOffset = viewDataManager.getViewDataDynamicOffset(data.viewIds[i]);
                    cmdList.setBindGroup(
                        1,
                        rhi::PipelineBindPoint::Graphics,
                        &rendererContext.getGlobalPipelineLayout(),
                        &rendererContext.getViewDataManager().getBindGroup(),
                        { &dynamicOffset, 1 }
                    );

                    setViewportAndScissor(cmdList, data.viewports[i]);

                    rhi::Pipeline* currentPipeline = nullptr;
                    const rhi::PipelineLayout* instanceBindLayout = nullptr;
                    MaterialProxy* prevMaterial = nullptr;
                    GeometryProxy* prevGeometry = nullptr;
                    for (const DrawBatch& drawBatch : data.shadowDepthBatches[i]->batches)
                    {
                        const DrawItem& batchDrawItem = (*data.drawItems[i])[drawBatch.itemIndex];
                        MaterialProxy* batchMaterial = batchDrawItem.material;

                        VertexLayout vertexLayout = batchDrawItem.getVertexLayout();
                        // 只对"真的会走共享 shader 路径、且是静态网格"的组合做归一化——非共享路径
                        // (有顶点位置偏移/Masked 等)、或骨骼网格(现在 writeVertexLayoutBindingsAndAttributes
                        // 还没实现 Skeletal_Mesh 分支, 归一化到 Static_Mesh 类型没有意义), 都保持用
                        // 网格自己真实的 vertexLayout, 行为和归一化之前一致
                        if (batchMaterial->usesSharedGeometryShader(renderTag) &&
                            vertexLayout.getType() == VertexLayoutType::Static_Mesh)
                        {
                            vertexLayout = getSharedShadowVertexLayout(vertexLayout);
                        }
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

                            // 这一级 cascade 的 depth bias(见 ShadowDepth.slang 的 ShadowDepthBiasParams)——
                            // pipeline 在同一级 cascade 内按材质切换时会重复发, 值不变, 无害, 不值得
                            // 为了去重再加状态跟踪
                            cmdList.setImmediateData(
                                &pipeline->getLayout(),
                                0,
                                std::span<const uint8_t>(
                                    reinterpret_cast<const uint8_t*>(&data.biases[i]),
                                    sizeof(data.biases[i])
                                )
                            );
                        }

                        if (batchMaterial != prevMaterial)
                        {
                            // 共享 shader 的 pipeline layout 根本没有材质参数这个 set,即便材质
                            // 自己的 mParametersBindGroup 因为要给 Gbuffer 用而非空,这里也不能绑
                            const rhi::BindGroup* parametersBindGroup = batchMaterial->usesSharedGeometryShader(renderTag)
                                ? nullptr
                                : batchMaterial->getParametersBindGroup();
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

                        batchDrawItem.draw(cmdList, drawBatch.instanceCount, cascadeFirstInstance[i] + drawBatch.firstInstance);
                    }
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

        const rhi::BindGroupLayout* instanceLayout = &rendererContext.getDrawBindGroupLayout(vertexLayout.getType());

        const bool usesSharedGeometryShader = material.usesSharedGeometryShader(renderTag);
        const rhi::BindGroup* materialParameterBindGroup = usesSharedGeometryShader ? nullptr : material.getParametersBindGroup();
        auto* pipelineLayout = createRenderPassPipelineLayout(
            rendererContext,
            nullptr,
            materialParameterBindGroup ? &materialParameterBindGroup->getLayout() : nullptr,
            instanceLayout
        );

        PipelineCache::GraphicsKey pipelineKey;
        // 这里用正向 Z 的 Less, 不是其它 pass 统一使用的 reversed-Z Greater_Equal——阴影贴图独立
        // 于主相机深度约定, 沿用的是传统正向深度比较
        pipelineKey.pipelineState = PipelineStatePresets::opaqueMesh(material.getAttributes().doubleSided, rhi::CompareOp::Less);
        // Keeps upstream casters (DirectionalLightProxy.cpp's casterCullingFrustum) from being near-clipped.
        pipelineKey.pipelineState.depthClampEnable = 1;
        pipelineKey.pipelineState.shader = shaderVariant;
        pipelineKey.pipelineState.vertexLayout = vertexLayout;
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::D16_UNORM>();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }
}
