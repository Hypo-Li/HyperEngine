#include "CardCapturePass.h"
#include "CardCaptureCopyPass.h"
#include "CardLightingResetPass.h"
#include "RenderPassUtils.h"
#include "../SurfaceCacheAtlas.h"
#include "../LumenScene.h"

#include <RenderBase/RenderBackend.h>
#include <RenderBase/MeshShapeProxy.h>

#include <Core/Common/LogSystem.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <optional>
#include <vector>

namespace hyp
{
    namespace
    {
        // 跟 Shader/Material/CardCapture.slang 里的 CardCaptureConstants 逐字段
        // 一一对应, 必须手动保持同步——两边都用独立标量摆放, 不用 float3/uint3, 见该文件
        // 顶部的说明
        struct CardCaptureConstants
        {
            float cardCenterX, cardCenterY, cardCenterZ;
            float cardHalfExtentsX, cardHalfExtentsY, cardHalfExtentsZ;
            uint32_t directionIndex;
            uint32_t pageIndexX;
            uint32_t pageIndexY;
            uint32_t pageCountX;
            uint32_t pageCountY;
            // 这一页在图集里实际占用的宽高(texel)——子页分配的卡这里会小于 kSurfaceCachePageSize,
            // shader 端拿这个而不是编译期常量去做 NDC<->texel 换算, 否则子页卡会把 NDC 铺满
            // 整页, 画到共享同一物理页的别的卡的区域里
            uint32_t pixelExtentX;
            uint32_t pixelExtentY;
        };

        rhi::Pipeline* getOrCreateCardCapturePipeline(
            RendererContext& rendererContext,
            MaterialProxy& material,
            VertexLayout vertexLayout)
        {
            HString renderTag = BuiltinRenderTags::CardCapture;
            rhi::Pipeline* pipeline = material.getCachedPipeline(renderTag, vertexLayout);
            if (pipeline)
                return pipeline;

            ShaderVariant shaderVariant = material.getOrCreateShaderVariant(renderTag, vertexLayout);
            auto& pipelineCache = rendererContext.getRenderBackend().getPipelineCache();

            const rhi::BindGroup* materialParametersBindGroup = material.getParametersBindGroup();
            auto* pipelineLayout = createRenderPassPipelineLayout(
                rendererContext,
                nullptr,
                materialParametersBindGroup ? &materialParametersBindGroup->getLayout() : nullptr
            );

            // 深度约定近=1远=0(见 CardCapture.slang VertexMain 的 ndcZ), 跟引擎其它 pass
            // 的 reversed-Z 一致, 所以复用同一个 Greater_Equal
            PipelineCache::GraphicsKey pipelineKey;
            pipelineKey.pipelineState = PipelineStatePresets::opaqueMesh(material.getAttributes().doubleSided, rhi::CompareOp::Greater_Equal);
            pipelineKey.pipelineState.shader = shaderVariant;
            pipelineKey.pipelineState.vertexLayout = vertexLayout;
            pipelineKey.pipelineState.attachmentFormats = AttachmentFormats::get<
                rhi::TextureFormat::D16_UNORM,
                rhi::TextureFormat::RGBA8_UNORM,
                rhi::TextureFormat::RGBA8_UNORM,
                rhi::TextureFormat::RGBA16_SFLOAT
            >();
            pipelineKey.pipelineLayout = pipelineLayout;

            pipeline = pipelineCache.getOrCreateGraphicsPipeline(pipelineKey);
            material.cachePipeline(renderTag, vertexLayout, pipeline);
            return pipeline;
        }

        struct CardPageTask
        {
            MeshRenderProxy* proxy;
            CardCaptureConstants constants;
            rhi::RenderArea renderArea;
        };

        struct CardCaptureData
        {
            std::vector<CardPageTask> tasks;
        };

        struct CardPageCaptureState
        {
            rhi::Pipeline* currentPipeline = nullptr;
            MaterialProxy* prevMaterial = nullptr;
            GeometryProxy* prevGeometry = nullptr;
        };

        void executeCardPageCapture(
            rg::ExecuteContext& context,
            RendererContext& rendererContext,
            const CardPageTask& task,
            CardPageCaptureState& state)
        {
            auto& cmdList = context.getCommandList();

            for (const DrawItem& drawItem : task.proxy->getDrawItemsLOD0())
            {
                VertexLayout vertexLayout = drawItem.geometry->getVertexLayout();
                rhi::Pipeline* pipeline = getOrCreateCardCapturePipeline(rendererContext, *drawItem.material, vertexLayout);
                if (pipeline != state.currentPipeline)
                {
                    cmdList.bindPipeline(pipeline);
                    state.currentPipeline = pipeline;
                    // 换了 pipeline 意味着 pipeline layout 可能也变了(不同材质的 set2 布局不同),
                    // Vulkan/WebGPU 规范都允许这种情况下把之前绑定的、跟新 layout 不兼容的
                    // descriptor set 隐式作废——material 的 set2 必须跟着重绑, 不能只靠
                    // "material 指针没变"判断上一份绑定还有效
                    state.prevMaterial = nullptr;
                }

                if (drawItem.material != state.prevMaterial)
                {
                    const rhi::BindGroup* parametersBindGroup = drawItem.material->getParametersBindGroup();
                    if (parametersBindGroup)
                    {
                        cmdList.setBindGroup(
                            2,
                            rhi::PipelineBindPoint::Graphics,
                            &state.currentPipeline->getLayout(),
                            parametersBindGroup,
                            {}
                        );
                    }
                    state.prevMaterial = drawItem.material;
                }

                cmdList.setImmediateData(
                    &state.currentPipeline->getLayout(),
                    0,
                    std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&task.constants), sizeof(task.constants))
                );

                if (drawItem.geometry != state.prevGeometry)
                {
                    drawItem.geometry->bind(cmdList);
                    state.prevGeometry = drawItem.geometry;
                }

                drawItem.geometry->draw(cmdList, drawItem.sectionIndex, 1, 0);
            }
        }

        constexpr uint32_t kSurfaceCacheCardRequestsPerFrame = 300; // 对应 UE r.LumenScene.SurfaceCache.CardCapturesPerFrame
        constexpr uint32_t kDistanceBinCount = 16;                  // 对应 UE Lumen::NumDistanceBuckets

        // 对应 UE Lumen::GetMeshCardDistanceBin, UE 的单位是厘米, 这里的距离是米
        uint32_t getDistanceBin(float distanceMeters) noexcept
        {
            const int32_t offsetDistance = std::max(1, static_cast<int32_t>(distanceMeters * 100.0f - 1000.0f));
            const uint32_t bin = static_cast<uint32_t>(std::bit_width(static_cast<uint32_t>(offsetDistance))) - 1u;
            return std::min(bin, kDistanceBinCount - 1u);
        }

        // 按距离分桶, 保留最近的 budget 个请求, 最后一个桶内按原顺序取
        void selectCardRequests(std::vector<SurfaceCacheCardRequest>& requests, uint32_t budget)
        {
            if (requests.size() <= budget)
                return;

            uint32_t histogram[kDistanceBinCount] = {};
            for (const SurfaceCacheCardRequest& request : requests)
                ++histogram[getDistanceBin(request.viewerDistance)];

            uint32_t lastBin = 0;
            uint32_t lastBinQuota = 0;
            uint32_t count = 0;
            for (; lastBin < kDistanceBinCount; ++lastBin)
            {
                if (count + histogram[lastBin] >= budget)
                {
                    lastBinQuota = budget - count;
                    break;
                }
                count += histogram[lastBin];
            }

            size_t writeIndex = 0;
            for (const SurfaceCacheCardRequest& request : requests)
            {
                const uint32_t bin = getDistanceBin(request.viewerDistance);
                if (bin < lastBin || (bin == lastBin && lastBinQuota > 0))
                {
                    if (bin == lastBin)
                        --lastBinQuota;
                    requests[writeIndex++] = request;
                }
            }
            requests.resize(writeIndex);
        }
        constexpr int32_t kSurfaceCacheCardCaptureFactor = 64; // 对应 UE r.LumenScene.SurfaceCache.CardCaptureFactor

        // 对应 UE FLumenSceneData::GetCardCaptureAtlasSizeInPages: 每帧可捕获的 texel 数为物理图集的 1/Factor,
        // 但至少放得下一张最大分辨率的卡
        int32_t computeCaptureAtlasResolution()
        {
            const float multPerComponent = 1.0f / std::sqrt(static_cast<float>(kSurfaceCacheCardCaptureFactor));
            const int32_t scaledResolution = static_cast<int32_t>(kSurfaceCacheAtlasResolution * multPerComponent + 0.5f);
            int32_t pagesPerAxis = (scaledResolution + kSurfaceCachePageSize - 1) / kSurfaceCachePageSize;
            pagesPerAxis = std::max(pagesPerAxis, (kSurfaceCacheCardMaxResolution + kSurfaceCachePageSize - 1) / kSurfaceCachePageSize);
            pagesPerAxis = std::min(pagesPerAxis, kSurfaceCacheAtlasPagesPerAxis);
            return pagesPerAxis * kSurfaceCachePageSize;
        }

        constexpr float kSurfaceCacheCardCaptureRefreshFraction = 0.125f; // 对应 UE r.LumenScene.SurfaceCache.CardCaptureRefreshFraction

        // 对应 UE ProcessLumenSurfaceCacheRequests 里的 PagesToRecaptureHeap 和 LastCapturedPageHeap 两段:
        // 先处理手动失效的页, 只受临时捕获图集空间限制; 再按最久没捕获的顺序刷新, 受刷新预算限制
        void recapturePages(
            SurfaceCacheRecaptureQueue& recaptureQueue,
            uint32_t frame,
            SurfaceCachePageAllocator& captureAllocator,
            int32_t captureAtlasResolution,
            std::vector<SurfaceCachePageCapture>& captures)
        {
            while (std::optional<uint32_t> recordId = recaptureQueue.peekInvalidated())
            {
                const SurfaceCacheRecaptureQueue::PageRef& page = recaptureQueue.getPage(*recordId);
                if (!page.proxy->recapturePage(page.cardIndex, page.pageLinearIndex, captureAllocator, captures))
                    break;
                recaptureQueue.markCaptured(*recordId, frame);
            }

            const int64_t captureAtlasTexels = static_cast<int64_t>(captureAtlasResolution) * captureAtlasResolution;
            int64_t texelsLeft = std::max<int64_t>(
                static_cast<int64_t>(static_cast<float>(captureAtlasTexels) * kSurfaceCacheCardCaptureRefreshFraction),
                static_cast<int64_t>(kSurfaceCachePageSize) * kSurfaceCachePageSize);
            int64_t pagesLeft = std::min<int64_t>(
                std::clamp<int64_t>(static_cast<int64_t>(kSurfaceCacheCardRequestsPerFrame * kSurfaceCacheCardCaptureRefreshFraction), 1, kSurfaceCacheCardRequestsPerFrame),
                static_cast<int64_t>(kSurfaceCacheCardRequestsPerFrame) - static_cast<int64_t>(captures.size()));

            while (std::optional<uint32_t> recordId = recaptureQueue.peekOldest())
            {
                if (recaptureQueue.getLastCapturedFrame(*recordId) == frame)
                    break;

                const SurfaceCacheRecaptureQueue::PageRef& page = recaptureQueue.getPage(*recordId);
                const SurfaceCachePageRegion atlasRegion = page.proxy->getCardPageRegion(page.cardIndex, page.pageLinearIndex);
                texelsLeft -= static_cast<int64_t>(atlasRegion.pixelExtent.x) * atlasRegion.pixelExtent.y;
                pagesLeft -= 1;
                if (texelsLeft < 0 || pagesLeft < 0)
                    break;

                if (!page.proxy->recapturePage(page.cardIndex, page.pageLinearIndex, captureAllocator, captures))
                    break;
                recaptureQueue.markCaptured(*recordId, frame);
            }
        }

        // 必须在本 pass 和 MeshSDFPass 之前跑完, 两者都依赖这里决定的 card 页分配
        std::vector<SurfaceCachePageCapture> updateMeshCardPages(
            LumenScene& lumenScene,
            std::span<MeshRenderProxy* const> meshRenderProxies,
            std::span<CameraProxy* const> cameraProxies,
            std::span<const InstanceData> instanceData,
            int32_t captureAtlasResolution)
        {
            std::vector<SurfaceCachePageCapture> captures;
            if (cameraProxies.empty())
                return captures;

            std::vector<double3> cameraWorldPositions;
            cameraWorldPositions.reserve(cameraProxies.size());
            for (CameraProxy* camera : cameraProxies)
            {
                cameraWorldPositions.push_back(camera->getWorldPosition());
            }

            std::vector<SurfaceCacheCardRequest> requests;
            for (MeshRenderProxy* proxy : meshRenderProxies)
            {
                MeshShapeProxy* meshShapeProxy = proxy->getMeshShapeProxy();
                if (!meshShapeProxy)
                    continue;

                std::span<const DrawItem> drawItems = proxy->getDrawItemsLOD0();
                if (drawItems.empty())
                    continue;

                const InstanceData& proxyInstanceData = instanceData[proxy->getInstanceIndex()];
                proxy->collectCardRequests(proxyInstanceData.objectToWorld, cameraWorldPositions, requests);
            }

            selectCardRequests(requests, kSurfaceCacheCardRequestsPerFrame);

            SurfaceCachePageAllocator captureAllocator(captureAtlasResolution);
            uint32_t failedCount = 0;
            for (const SurfaceCacheCardRequest& request : requests)
            {
                const SurfaceCacheCardRequestResult result = request.proxy->applyCardRequest(request.cardIndex, request.resolution, captureAllocator, captures);
                if (result == SurfaceCacheCardRequestResult::Failed)
                    ++failedCount;
            }
            if (failedCount > 0)
                LOG_WARN("CardCapturePass: {}/{} card 分配失败(SurfaceCacheAtlas 页池或页表已满), 下一帧重试", failedCount, requests.size());

            recapturePages(lumenScene.getSurfaceCacheRecaptureQueue(), lumenScene.getSurfaceCacheUpdateFrameIndex(), captureAllocator, captureAtlasResolution, captures);

            return captures;
        }
    }

    void CardCapturePass::addToRenderGraph(
        rg::RenderGraph& renderGraph,
        RendererContext& rendererContext,
        const SceneProxy& scene
    )
    {
        LumenScene* lumenScene = scene.getLumenScene();
        if (!lumenScene)
            return;
        SurfaceCacheAtlas& surfaceCacheAtlas = lumenScene->getSurfaceCacheAtlas();

        lumenScene->advanceSurfaceCacheUpdateFrameIndex();
        const int32_t captureAtlasResolution = computeCaptureAtlasResolution();
        std::vector<SurfaceCachePageCapture> captures = updateMeshCardPages(
            *lumenScene, scene.getMeshRenderProxies(), scene.getCameraProxies(), scene.getInstanceData(), captureAtlasResolution);
        if (captures.empty())
            return;

        std::vector<CardPageTask> tasks;
        std::vector<CardCaptureCopyTask> copyTasks;
        std::vector<CardCaptureCopyTask> lightingResetTasks;
        tasks.reserve(captures.size());
        copyTasks.reserve(captures.size());
        for (const SurfaceCachePageCapture& capture : captures)
        {
            const MeshCard& card = capture.proxy->getMeshShapeProxy()->getCards()[capture.cardIndex];

            CardPageTask task;
            task.proxy = capture.proxy;
            task.constants = CardCaptureConstants{};
            task.constants.cardCenterX = card.center.x;
            task.constants.cardCenterY = card.center.y;
            task.constants.cardCenterZ = card.center.z;
            task.constants.cardHalfExtentsX = card.halfExtents.x;
            task.constants.cardHalfExtentsY = card.halfExtents.y;
            task.constants.cardHalfExtentsZ = card.halfExtents.z;
            task.constants.directionIndex = card.directionIndex;
            task.constants.pageIndexX = uint32_t(capture.pageIndex.x);
            task.constants.pageIndexY = uint32_t(capture.pageIndex.y);
            task.constants.pageCountX = uint32_t(capture.pageCount.x);
            task.constants.pageCountY = uint32_t(capture.pageCount.y);
            task.constants.pixelExtentX = uint32_t(capture.captureRegion.pixelExtent.x);
            task.constants.pixelExtentY = uint32_t(capture.captureRegion.pixelExtent.y);
            task.renderArea = rhi::RenderArea{
                capture.captureRegion.pixelOrigin.x, capture.captureRegion.pixelOrigin.y,
                uint32_t(capture.captureRegion.pixelExtent.x), uint32_t(capture.captureRegion.pixelExtent.y)
            };
            tasks.push_back(task);

            const CardCaptureCopyTask copyTask{
                capture.captureRegion.pixelOrigin,
                capture.atlasRegion.pixelOrigin,
                capture.captureRegion.pixelExtent
            };
            copyTasks.push_back(copyTask);
            if (capture.isNewAllocation)
                lightingResetTasks.push_back(copyTask);
        }

        CardCaptureTextures captureTextures;
        renderGraph.addGraphicsPass<CardCaptureData>(
            "CardCapture"_hs,
            [&](rg::RenderGraph::Builder& builder, CardCaptureData& data)
            {
                builder.markSideEffect();

                data.tasks = std::move(tasks);

                rhi::TextureDesc desc;
                desc.type = rhi::TextureType::Texture_2D;
                desc.extent = { uint32_t(captureAtlasResolution), uint32_t(captureAtlasResolution), 1 };
                desc.mipCount = 1;
                desc.layerCount = 1;

                desc.format = rhi::TextureFormat::RGBA8_UNORM;
                desc.usage = rhi::TextureUsage::Color_Attachment | rhi::TextureUsage::Sampled;
                rg::TextureHandle albedo = builder.createTexture("CardCaptureAlbedo"_hs, desc);
                rg::TextureHandle normal = builder.createTexture("CardCaptureNormal"_hs, desc);

                desc.format = rhi::TextureFormat::RGBA16_SFLOAT;
                rg::TextureHandle emissive = builder.createTexture("CardCaptureEmissive"_hs, desc);

                desc.format = rhi::TextureFormat::D16_UNORM;
                desc.usage = rhi::TextureUsage::Depth_Stencil_Attachment | rhi::TextureUsage::Sampled;
                rg::TextureHandle depth = builder.createTexture("CardCaptureDepth"_hs, desc);

                builder.setRenderArea(rhi::RenderArea{ 0, 0, uint32_t(captureAtlasResolution), uint32_t(captureAtlasResolution) });
                // 顺序必须跟 CardCapture.slang 的 SV_Target0/1/2 对应; 法线清成 (0,0,1) 的编码值, 深度清成最远
                captureTextures.albedo = builder.addColorAttachment(albedo, rhi::AttachmentLoadOp::Clear, rhi::ClearColorValue(float4(0.0f, 0.0f, 0.0f, 1.0f)));
                captureTextures.normal = builder.addColorAttachment(normal, rhi::AttachmentLoadOp::Clear, rhi::ClearColorValue(float4(0.5f, 0.5f, 1.0f, 1.0f)));
                captureTextures.emissive = builder.addColorAttachment(emissive, rhi::AttachmentLoadOp::Clear, rhi::ClearColorValue(float4(0.0f, 0.0f, 0.0f, 1.0f)));
                captureTextures.depth = builder.addDepthAttachment(depth, rhi::AttachmentLoadOp::Clear, true, 0.0f);
            },
            [&rendererContext](rg::ExecuteContext& context, CardCaptureData& data)
            {
                auto& cmdList = context.getCommandList();

                CardPageCaptureState captureState;
                for (const CardPageTask& task : data.tasks)
                {
                    const rhi::RenderArea& area = task.renderArea;
                    cmdList.setViewport(float(area.x), float(area.y), float(area.width), float(area.height), 0.0f, 1.0f);
                    cmdList.setScissor(area.x, area.y, area.width, area.height);
                    executeCardPageCapture(context, rendererContext, task, captureState);
                }
            }
        );

        CardCaptureCopyPass::addToRenderGraph(renderGraph, rendererContext, surfaceCacheAtlas, captureTextures, std::move(copyTasks));
        if (!lightingResetTasks.empty())
            CardLightingResetPass::addToRenderGraph(renderGraph, rendererContext, surfaceCacheAtlas, captureTextures.emissive, std::move(lightingResetTasks));
    }
}
