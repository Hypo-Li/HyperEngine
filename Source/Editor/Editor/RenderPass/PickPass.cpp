#include "PickPass.h"

#include <Renderer/MeshRenderProxy.h>
#include <Renderer/QuadRenderProxy.h>
#include <Renderer/SkinningManager.h>
#include <Renderer/RenderPass/RenderPassUtils.h>

#include <RenderBase/ProxyManager.h>
#include <RenderBase/PipelineCache.h>
#include <RenderBase/PipelineState.h>
#include <RenderBase/StagingAllocator.h>
#include <RenderBase/DeferredTaskQueue.h>

#include <cstring>
#include <algorithm>
#include <limits>

namespace hyp::ed
{
    const HString EditorRenderTags::Pick = "EditorPick";

    namespace
    {
        // 解析出来、且能在 LOD0 里找到对应 submesh 的候选——proxyHandle 失效/subIndex 越界的
        // 候选直接跳过, 不当错误处理(请求发起和这一帧执行之间, 对应 Component 可能已经被销毁,
        // 或者 mesh 被换成了 submesh 更少的另一个)
        struct ResolvedCandidate
        {
            DrawItem drawItem;
            uint32_t pickId;
        };
    }

    struct PickDrawData
    {
        std::vector<ResolvedCandidate> resolved;
        const SceneProxy* scene;
        uint32_t viewId;
        float4 viewport;
        rg::TextureHandle pickIdTarget;
        rg::TextureHandle pickDepth;
    };

    // WebGPU 要求 copyTextureToBuffer 的 bytesPerRow 是 256 的倍数
    static uint32_t getReadbackRowPitch(int32_t regionWidth)
    {
        constexpr uint32_t kTextureRowAlignment = 256;
        return (uint32_t(regionWidth) * sizeof(uint32_t) + kTextureRowAlignment - 1) / kTextureRowAlignment * kTextureRowAlignment;
    }

    struct PickReadbackData
    {
        rg::BufferHandle readback;
        rg::TextureHandle pickIdSource;
        // 回读区域左上角(纹理像素坐标)、区域尺寸, 以及点击点在这个区域内的局部坐标——区域尺寸
        // 在极小 viewport 下可能被裁到比 kPickToleranceRadiusPixels*2+1 还小, 不能假设恒定为
        // 那个值, 见 addToRenderGraph() 里的裁剪逻辑
        int32_t regionOriginX;
        int32_t regionOriginY;
        int32_t regionWidth;
        int32_t regionHeight;
        int32_t clickLocalX;
        int32_t clickLocalY;
        TRef<rhi::Buffer> readbackBuffer;
        std::shared_ptr<std::promise<uint32_t>> promise;
    };

    static rhi::Pipeline* createPipeline(
        RendererContext& rendererContext,
        MaterialProxy& material,
        VertexLayout vertexLayout)
    {
        auto shaderVariant = material.getOrCreateShaderVariant(EditorRenderTags::Pick, vertexLayout);
        auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

        const rhi::BindGroupLayout* instanceLayout = &rendererContext.getDrawBindGroupLayout(vertexLayout.getType());

        const bool usesSharedGeometryShader = material.usesSharedGeometryShader(EditorRenderTags::Pick);
        const rhi::BindGroup* materialParameterBindGroup = usesSharedGeometryShader ? nullptr : material.getParametersBindGroup();
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
        pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<rhi::TextureFormat::D32_SFLOAT, rhi::TextureFormat::R32_UINT>();
        pipelineKey.pipelineLayout = pipelineLayout;
        return pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
    }

    void PickPass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene,
        const SceneView& sceneView,
        const CameraProxy& camera,
        std::vector<PickCandidate> candidates,
        int2 clickPixelPos,
        std::shared_ptr<std::promise<uint32_t>> promise)
    {
        auto& proxyManager = rendererContext.getRenderBackend().getProxyManager();

        std::vector<ResolvedCandidate> resolved;
        resolved.reserve(candidates.size());
        for (const PickCandidate& candidate : candidates)
        {
            std::span<const DrawItem> drawItems;
            if (candidate.quadProxyHandle.isValid())
            {
                QuadRenderProxy* quadProxy = proxyManager.getProxy(candidate.quadProxyHandle);
                if (!quadProxy)
                    continue;
                drawItems = quadProxy->getDrawItems(sceneView);
            }
            else
            {
                MeshRenderProxy* proxy = proxyManager.getProxy(candidate.proxyHandle);
                if (!proxy)
                    continue;
                drawItems = proxy->getDrawItemsLOD0();
            }
            if (candidate.subIndex >= drawItems.size())
                continue;

            resolved.push_back(ResolvedCandidate{ drawItems[candidate.subIndex], candidate.pickId });
        }

        if (resolved.empty())
        {
            promise->set_value(0);
            return;
        }

        uint32_t viewId = sceneView.getViewId();
        float4 viewport = sceneView.getViewport();
        uint32_t width = uint32_t(viewport.z);
        uint32_t height = uint32_t(viewport.w);

        int2 clampedClickPos = {
            std::clamp(clickPixelPos.x, 0, int32_t(width) - 1),
            std::clamp(clickPixelPos.y, 0, int32_t(height) - 1)
        };

        // 回读区域: 点击点周围 kPickToleranceRadiusPixels 半径的一小块, 不是单个像素——见
        // PickPass.h 里 kPickToleranceRadiusPixels 的注释(UE HitProxy 那套"点击容差"的思路)。
        // viewport 本身比这块区域还小(理论上不太可能, 但不假设)时裁到 viewport 尺寸。
        constexpr int32_t kRegionSize = kPickToleranceRadiusPixels * 2 + 1;
        int32_t regionWidth = std::min<int32_t>(kRegionSize, int32_t(width));
        int32_t regionHeight = std::min<int32_t>(kRegionSize, int32_t(height));
        int32_t regionOriginX = std::clamp(clampedClickPos.x - kPickToleranceRadiusPixels, 0, int32_t(width) - regionWidth);
        int32_t regionOriginY = std::clamp(clampedClickPos.y - kPickToleranceRadiusPixels, 0, int32_t(height) - regionHeight);
        int32_t clickLocalX = clampedClickPos.x - regionOriginX;
        int32_t clickLocalY = clampedClickPos.y - regionOriginY;

        rg::TextureHandle pickIdTexture;

        renderGraph.addGraphicsPass<PickDrawData>(
            "EditorPick"_hs,
            [&](rg::RenderGraph::Builder& builder, PickDrawData& data)
            {
                data.resolved = resolved;
                data.scene = &scene;
                data.viewId = viewId;
                data.viewport = viewport;

                rhi::TextureDesc idDesc;
                idDesc.type = rhi::TextureType::Texture_2D;
                idDesc.format = rhi::TextureFormat::R32_UINT;
                idDesc.extent = { width, height, 1 };
                idDesc.mipCount = 1;
                idDesc.layerCount = 1;
                idDesc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Transfer_Src;
                pickIdTexture = builder.createTexture("EditorPickId"_hs(viewId), idDesc);

                rhi::TextureDesc depthDesc;
                depthDesc.type = rhi::TextureType::Texture_2D;
                depthDesc.format = rhi::TextureFormat::D32_SFLOAT;
                depthDesc.extent = { width, height, 1 };
                depthDesc.mipCount = 1;
                depthDesc.layerCount = 1;
                depthDesc.usage = rhi::TextureUsage::Depth_Stencil_Attachment;
                auto pickDepthTexture = builder.createTexture("EditorPickDepth"_hs(viewId), depthDesc);

                data.pickIdTarget = builder.addColorAttachment(pickIdTexture, rhi::AttachmentLoadOp::Clear);
                data.pickDepth = builder.addDepthStencilAttachment(pickDepthTexture, rhi::AttachmentLoadOp::Clear, true, rhi::AttachmentLoadOp::Dont_Care, false);

                // addColorAttachment() 返回的才是"写入之后"的那份句柄——后面 EditorPickReadback
                // pass 要读的是这份, 不是 createTexture() 刚建出来、还没被任何 pass 写过的原始
                // 句柄, 不然 Builder::transfer() 会断言失败(RenderGraph.cpp:209, "只有导入的
                // 资源才能未写先读"), 跟 GbufferPass 把 addColorAttachment() 的返回值存进
                // blackboard、而不是原始 createTexture() 句柄是同一个道理。
                pickIdTexture = data.pickIdTarget;
            },
            [&rendererContext](rg::ExecuteContext& context, PickDrawData& data)
            {
                auto& cmdList = context.getCommandList();

                std::vector<uint32_t> instanceIndices;
                instanceIndices.reserve(data.resolved.size());
                for (const auto& candidate : data.resolved)
                    instanceIndices.push_back(candidate.drawItem.instanceIndex);

                if (!rendererContext.getIndicesBuffer().upload(cmdList, instanceIndices))
                {
                    context.beginPass();
                    return;
                }

                context.beginPass();

                setViewportAndScissor(cmdList, data.viewport);

                uint32_t dynamicOffset = rendererContext.getViewDataManager().getViewDataDynamicOffset(data.viewId);
                cmdList.setBindGroup(
                    1,
                    rhi::PipelineBindPoint::Graphics,
                    &rendererContext.getGlobalPipelineLayout(),
                    &rendererContext.getViewDataManager().getBindGroup(),
                    { &dynamicOffset, 1 }
                );

                rhi::Pipeline* currentPipeline = nullptr;
                const rhi::PipelineLayout* instanceBindLayout = nullptr;
                MaterialProxy* prevMaterial = nullptr;
                GeometryProxy* prevGeometry = nullptr;

                for (uint32_t i = 0; i < data.resolved.size(); ++i)
                {
                    const auto& candidate = data.resolved[i];
                    const DrawItem& drawItem = candidate.drawItem;
                    VertexLayout vertexLayout = drawItem.getVertexLayout();

                    rhi::Pipeline* pipeline = drawItem.material->getCachedPipeline(EditorRenderTags::Pick, vertexLayout);
                    if (!pipeline)
                    {
                        pipeline = createPipeline(rendererContext, *drawItem.material, vertexLayout);
                        drawItem.material->cachePipeline(EditorRenderTags::Pick, vertexLayout, pipeline);
                    }
                    if (pipeline != currentPipeline)
                    {
                        cmdList.bindPipeline(pipeline);
                        currentPipeline = pipeline;
                    }

                    if (drawItem.material != prevMaterial)
                    {
                        const rhi::BindGroup* parametersBindGroup = drawItem.material->usesSharedGeometryShader(EditorRenderTags::Pick)
                            ? nullptr
                            : drawItem.material->getParametersBindGroup();
                        if (parametersBindGroup)
                        {
                            cmdList.setBindGroup(2, rhi::PipelineBindPoint::Graphics, &currentPipeline->getLayout(), parametersBindGroup, {});
                        }
                        prevMaterial = drawItem.material;
                    }

                    // set3 必须排在 set2 之后绑, 否则 set2 换了布局会让它失效
                    if (&currentPipeline->getLayout() != instanceBindLayout)
                    {
                        const rhi::BindGroup& instanceBindGroup = data.scene->getDrawBindGroup(vertexLayout.getType());
                        cmdList.setBindGroup(3, rhi::PipelineBindPoint::Graphics, &currentPipeline->getLayout(), &instanceBindGroup, {});
                        instanceBindLayout = &currentPipeline->getLayout();
                    }

                    if (drawItem.geometry != prevGeometry)
                    {
                        drawItem.bind(cmdList);
                        prevGeometry = drawItem.geometry;
                    }

                    struct EditorPickPushConstants
                    {
                        uint32_t pickId;
                    };
                    EditorPickPushConstants pushConstants{ candidate.pickId };
                    cmdList.setImmediateData(&currentPipeline->getLayout(), 0, std::span(reinterpret_cast<const uint8_t*>(&pushConstants), sizeof(pushConstants)));

                    drawItem.draw(cmdList, 1, i);
                }
            },
            rg::PassFlags::Skip_Begin_Pass
        );

        rhi::BufferDesc readbackDesc;
        readbackDesc.size = size_t(getReadbackRowPitch(regionWidth)) * size_t(regionHeight);
        readbackDesc.usage = rhi::BufferUsage::Transfer_Dst;
        TRef<rhi::Buffer> readbackBuffer = rendererContext.getRenderBackend().getDevice().createBuffer(readbackDesc, rhi::MemoryType::Gpu_To_Cpu);

        renderGraph.addTransferPass<PickReadbackData>(
            "EditorPickReadback"_hs,
            [&, readbackBuffer, regionOriginX, regionOriginY, regionWidth, regionHeight, clickLocalX, clickLocalY,
            promise = std::move(promise)](rg::RenderGraph::Builder& builder, PickReadbackData& data) mutable
            {
                builder.markSideEffect();

                data.regionOriginX = regionOriginX;
                data.regionOriginY = regionOriginY;
                data.regionWidth = regionWidth;
                data.regionHeight = regionHeight;
                data.clickLocalX = clickLocalX;
                data.clickLocalY = clickLocalY;
                data.readbackBuffer = readbackBuffer;
                data.promise = std::move(promise);

                data.pickIdSource = builder.transfer(pickIdTexture, rg::AccessType::Read_Only);

                auto readback = builder.importBuffer("EditorPickReadbackBuffer"_hs(viewId), readbackBuffer.get());
                data.readback = builder.accessBuffer(readback, rhi::BufferUsage::Transfer_Dst, rg::AccessType::Write_Only);
            },
            [&rendererContext](rg::ExecuteContext& context, PickReadbackData& data) mutable
            {
                auto& cmdList = context.getCommandList();
                rhi::TextureView* pickIdView = context.getTexture(data.pickIdSource);
                rhi::Buffer* readback = context.getBuffer(data.readback);

                rhi::BufferTextureCopyRegion region;
                region.bufferOffset = 0;
                region.bytesPerRow = getReadbackRowPitch(data.regionWidth);
                region.rowsPerTexture = uint32_t(data.regionHeight);
                region.slice = { rhi::TextureAspect::Color, 0, 0, 1 };
                region.textureOffset = { uint32_t(data.regionOriginX), uint32_t(data.regionOriginY), 0 };
                region.textureExtent = { uint32_t(data.regionWidth), uint32_t(data.regionHeight), 1 };
                cmdList.copyTextureToBuffer(&pickIdView->getTexture(), readback, { &region, 1 });

                rendererContext.getRenderBackend().getDeferredTaskQueue().push(
                    [readback = std::move(data.readbackBuffer), promise = std::move(data.promise),
                    regionWidth = data.regionWidth, regionHeight = data.regionHeight,
                    clickLocalX = data.clickLocalX, clickLocalY = data.clickLocalY]() mutable
                    {
                        uint32_t pickId = 0;
                        const uint8_t* mapped = static_cast<const uint8_t*>(readback->map());
                        if (mapped)
                        {
                            // UE HitProxy 拾取同样的思路: 不是只认点击的那一个像素, 而是在这块
                            // 小区域里找离点击点最近的非 0 命中——细线/小物体在屏幕上可能一个像素
                            // 都不精确落在点击点上, 但会落在这个容差范围内的某个像素上。
                            int64_t bestDistSq = std::numeric_limits<int64_t>::max();
                            for (int32_t y = 0; y < regionHeight; ++y)
                            {
                                const uint32_t* row = reinterpret_cast<const uint32_t*>(mapped + size_t(y) * getReadbackRowPitch(regionWidth));
                                for (int32_t x = 0; x < regionWidth; ++x)
                                {
                                    uint32_t candidatePickId = row[x];
                                    if (candidatePickId == 0)
                                        continue;
                                    int64_t dx = x - clickLocalX;
                                    int64_t dy = y - clickLocalY;
                                    int64_t distSq = dx * dx + dy * dy;
                                    if (distSq < bestDistSq)
                                    {
                                        bestDistSq = distSq;
                                        pickId = candidatePickId;
                                    }
                                }
                            }
                            readback->unmap();
                        }
                        promise->set_value(pickId);
                    },
                    kMaxFramesInFlight
                );
            }
        );
    }
}
