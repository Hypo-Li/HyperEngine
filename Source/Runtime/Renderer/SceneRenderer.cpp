#include "SceneRenderer.h"
#include "MeshRenderProxy.h"
#include "RenderPipeline.h"

#include <cassert>
#include <execution>
#include <ranges>

namespace hyp
{
    SceneRenderer::SceneRenderer(RendererContext& rendererContext) :
        mRendererContext(rendererContext)
    {
    }

    struct UploadData
    {
        SceneProxy* scene;
        // RenderTargets 需要初始化为 Sampled 布局
        std::vector<TextureProxy*> renderTargets;
        std::vector<const CameraProxy*> cameras;
        ViewDataManager* viewDataManager;
        std::vector<uint32_t> viewIds;
        std::vector<ViewData> viewDatas;
    };

    void SceneRenderer::render(SceneProxy& scene, std::span<const SceneViewGroup> sceneViewGroups, rg::RenderGraph& renderGraph)
    {
        size_t sceneViewCount = 0;
        for (auto& sceneViewGroup : sceneViewGroups)
            sceneViewCount += sceneViewGroup.getSceneViews().size();
        std::vector<RenderData> renderData(sceneViewCount);

        std::vector<const SceneView*> sceneViewPtrs;
        sceneViewPtrs.reserve(sceneViewCount);
        for (auto& sceneViewGroup : sceneViewGroups)
        {
            for (auto& sceneView : sceneViewGroup.getSceneViews())
            {
                sceneViewPtrs.push_back(&sceneView);
            }
        }

#define USE_PARALLEL_CULL 1
#if USE_PARALLEL_CULL
        if (sceneViewCount > 4)
        {
            auto range = std::views::iota(size_t(0), sceneViewCount);
            // NOTE: GCC need link Intel TBB
            std::for_each(std::execution::par, range.begin(), range.end(),
                [&](size_t i)
                {
                    cull(scene, *sceneViewPtrs[i], renderData[i]);
                }
            );
        }
        else
#endif
        {
            for (size_t i = 0; i < sceneViewPtrs.size(); i++)
            {
                cull(scene, *sceneViewPtrs[i], renderData[i]);
            }
        }

        renderGraph.addTransferPass<UploadData>(
            "Upload"_hs,
            [&](rg::RenderGraph::Builder& builder, UploadData& data)
            {
                builder.markSideEffect();

                data.scene = &scene;
                scene.update();

                // collect renderTargetProxies and cameraProxies
                data.renderTargets.reserve(sceneViewGroups.size());
                data.cameras.reserve(sceneViewGroups.size());
                for (auto& sceneViewGroup : sceneViewGroups)
                {
                    if (sceneViewGroup.getType() != SceneViewGroup::Type::Camera)
                        continue;
                    const CameraProxy* cameraProxy = sceneViewGroup.getViewSource<CameraViewSource>();
                    if (!cameraProxy)
                        continue;
                    if (cameraProxy->getRenderTarget())
                        data.renderTargets.push_back(cameraProxy->getRenderTarget());
                    data.cameras.push_back(cameraProxy);
                }

                data.viewDataManager = &mRendererContext.getViewDataManager();
                data.viewIds.reserve(sceneViewCount);
                data.viewDatas.reserve(sceneViewCount);
                for (auto& sceneView : sceneViewPtrs)
                {
                    data.viewIds.emplace_back(sceneView->getViewId());
                    data.viewDatas.emplace_back(sceneView->makeViewData());
                }
            },
            [](rg::ExecuteContext& context, UploadData& data)
            {
                auto& cmdList = context.getCommandList();
                data.scene->prepare(cmdList);

                for (TextureProxy* renderTarget : data.renderTargets)
                {
                    renderTarget->prepare(cmdList);
                }

                for (const CameraProxy* cameraProxy : data.cameras)
                {
                    PostProcessState& postProcess = cameraProxy->getPostProcessState();
                    postProcess.prepareColorGradingLut(cmdList);
                    postProcess.prepareAutoExposureData(cmdList);
                    postProcess.prepareTAAHistory(cmdList);
                    postProcess.prepareAOHistory(cmdList);
                    postProcess.prepareSceneDepthHistory(cmdList);
                    postProcess.prepareDiffuseIndirectHistory(cmdList);
                    postProcess.prepareShortRangeAOHistory(cmdList);
                    postProcess.prepareRoughSpecularIndirectHistory(cmdList);
                    postProcess.prepareScreenProbeLightingHistory(cmdList);
                    postProcess.prepareReflectionHistory(cmdList);
                }

                context.getRenderBackend().getPrepareQueue().flush(cmdList);

                data.viewDataManager->updateViewData(cmdList, data.viewIds, data.viewDatas);
            }
        );

        size_t renderDataIndex = 0;
        for (auto& sceneViewGroup : sceneViewGroups)
        {
            switch (sceneViewGroup.getType())
            {
            case SceneViewGroup::Type::Camera:
            {
                auto viewSource = sceneViewGroup.getViewSource<CameraViewSource>();
                DeferredPipeline::addToRenderGraph(
                    renderGraph,
                    mRendererContext,
                    scene,
                    sceneViewGroup.getSceneViews()[0],
                    renderData[renderDataIndex],
                    *viewSource
                );
                ++renderDataIndex;
                break;
            }
            case SceneViewGroup::Type::Cascade_Shadow:
            {
                auto sceneViews = sceneViewGroup.getSceneViews();
                auto viewSource = sceneViewGroup.getViewSource<CascadeShadowViewSource>();
                CascadeShadowPipeline::addToRenderGraph(
                    renderGraph,
                    mRendererContext,
                    scene,
                    sceneViews,
                    { &renderData[renderDataIndex], sceneViews.size() },
                    *viewSource.directionalLight,
                    *viewSource.camera
                );
                renderDataIndex += sceneViews.size();
                break;
            }
            default:
                break;
            }
        }
    }

    uint64_t SceneRenderer::makeOpaqueSortKey(
        uint32_t materialId,
        uint32_t vertexLayoutId,
        uint32_t geometryId,
        uint32_t sectionIndex,
        float distance,
        float maxDistance
    )
    {
        uint64_t sortKey = 0;
        sortKey |= (static_cast<uint64_t>(materialId) & 0xFFFFF) << 44;     // 20 bit
        sortKey |= (static_cast<uint64_t>(vertexLayoutId) & 0xFF) << 36;    // 8 bit
        sortKey |= (static_cast<uint64_t>(geometryId) & 0xFFFFF) << 16;     // 20 bit
        sortKey |= (static_cast<uint64_t>(sectionIndex) & 0xF) << 12;       // 4 bit
        sortKey |= static_cast<uint64_t>((std::min(distance, maxDistance) / maxDistance) * 4096) & 0xFFF; // 12 bit
        return sortKey;
    }

    uint64_t SceneRenderer::makeTranslucentSortKey(
        int8_t sortOrder,
        float distance,
        uint32_t materialId,
        uint32_t geometryId
    )
    {
        uint64_t sortKey = 0;
        sortKey |= (static_cast<uint64_t>(sortOrder) + 128) << 56;
        uint32_t invDistBits = 0xFFFFFFFFu - std::bit_cast<uint32_t>(distance);
        sortKey |= static_cast<uint64_t>(invDistBits) << 24;
        sortKey |= (static_cast<uint64_t>(materialId) & 0xFFF) << 12;
        sortKey |= (static_cast<uint64_t>(geometryId) & 0xFFF);

        return sortKey;
    }

    uint64_t SceneRenderer::makeDepthOnlySortKey(
        BlendMode blendMode,
        bool hasVertexPositionOffset,
        VertexLayoutType vertexLayoutType,
        uint32_t materialId,
        uint32_t vertexLayoutId,
        uint32_t geometryId,
        uint32_t sectionIndex,
        float distance,
        float maxDistance
    )
    {
        uint64_t sortKey = 0;
        bool isStaticOpaque = (blendMode == BlendMode::Opaque &&
            vertexLayoutType == VertexLayoutType::Static_Mesh &&
            !hasVertexPositionOffset);
        sortKey |= static_cast<uint64_t>(isStaticOpaque ? 0 : 1) << 63;     // 1 bit
        sortKey |= (static_cast<uint64_t>(vertexLayoutId) & 0xFF) << 55;    // 8 bit
        materialId = isStaticOpaque ? 0 : materialId;
        sortKey |= (static_cast<uint64_t>(materialId) & 0xFFFFF) << 35;     // 20 bit
        sortKey |= (static_cast<uint64_t>(geometryId) & 0xFFFFF) << 15;     // 20 bit
        sortKey |= (static_cast<uint64_t>(sectionIndex) & 0xF) << 11;       // 4 bit
        sortKey |= static_cast<uint64_t>((std::min(distance, maxDistance) / maxDistance) * 32767) & 0x7FF; // 11 bit
        return sortKey;
    }

    void SceneRenderer::cull(const SceneProxy& scene, const SceneView& sceneView, RenderData& renderData)
    {
        auto& renderTagRegistry = mRendererContext.getRenderBackend().getRenderTagRegistry();
        uint32_t viewMask = sceneView.getRenderMask();
        const Frustumd& frustum = sceneView.getFrustum();
        auto cullDrawable = [&](const DrawableProxy* drawableProxy)
        {
            uint32_t layerMask = drawableProxy->getLayerMask();
            if (!drawableProxy->isVisible() || !(sceneView.getCullingMask() & layerMask))
                return;

            if (!frustum.contains(drawableProxy->getBoundingSphere()))
                return;

            if (!frustum.contains(drawableProxy->getBoundingBox()))
                return;

            uint32_t drawableMask = drawableProxy->getRenderMask();
            int8_t sortOrder = drawableProxy->getSortOrder();

            double3 meshWorldPos = drawableProxy->getBoundingSphere().getCenter();
            double3 cameraWorldPos = sceneView.getCameraOrigin();
            double3 distanceVec = meshWorldPos - cameraWorldPos;
            float distanceSqr = static_cast<float>(glm::dot(distanceVec, distanceVec));
            // Drawable 内部根据 View 在内部查询合适的 LOD, 并返回对应的 DrawItem
            for (const auto& drawItem : drawableProxy->getDrawItems(sceneView))
            {
                uint32_t materialMask = drawItem.material->getRenderMask();
                uint32_t combinedMask = (materialMask & drawableMask) & viewMask;
                const bool inPreDepth = combinedMask & renderTagRegistry.getBuiltinDepthOnlyMask();
                const bool inOpaque = combinedMask & renderTagRegistry.getBuiltinOpaqueMask();
                const bool inTranslucent = !inOpaque && (combinedMask & renderTagRegistry.getBuiltinTranslucentMask());
                const bool inShadowDepth = combinedMask & renderTagRegistry.getBuiltinShadowDepthMask();
                if (!inPreDepth && !inOpaque && !inTranslucent && !inShadowDepth)
                    continue;

                const uint32_t itemIndex = static_cast<uint32_t>(renderData.drawItems->size());
                renderData.drawItems->push_back(drawItem);

                BlendMode blendMode = drawItem.material->getAttributes().blendMode;
                bool hasVertexPositionOffset = drawItem.material->getAttributes().hasVertexPositionOffset;
                uint32_t materialId = drawItem.material->getId();
                uint32_t vertexLayoutId = drawItem.getVertexLayout().getId();
                VertexLayoutType vertexLayoutType = drawItem.getVertexLayout().getType();
                uint32_t geometryId = drawItem.getGeometryId();
                uint32_t sectionIndex = drawItem.sectionIndex;

                static constexpr float kMaxDistanceSqr = 1000.0f * 1000.0f;

                if (inPreDepth)
                {
                    uint64_t preDepthSortKey = makeDepthOnlySortKey(blendMode, hasVertexPositionOffset, vertexLayoutType, materialId, vertexLayoutId, geometryId, sectionIndex, distanceSqr, kMaxDistanceSqr);
                    renderData.preDepthBucket->push_back({ preDepthSortKey, itemIndex });
                }

                if (inOpaque)
                {
                    uint64_t opaqueSortKey = makeOpaqueSortKey(materialId, vertexLayoutId, geometryId, sectionIndex, distanceSqr, kMaxDistanceSqr);
                    renderData.opaqueBucket->push_back({ opaqueSortKey, itemIndex });
                }
                else if (inTranslucent)
                {
                    uint64_t translucentSortKey = makeTranslucentSortKey(sortOrder, distanceSqr, materialId, geometryId);
                    renderData.translucentBucket->push_back({ translucentSortKey, itemIndex });
                }

                if (inShadowDepth)
                {
                    uint64_t shadowDepthSortKey = makeDepthOnlySortKey(blendMode, hasVertexPositionOffset, vertexLayoutType, materialId, vertexLayoutId, geometryId, sectionIndex, distanceSqr, kMaxDistanceSqr);
                    renderData.shadowDepthBucket->push_back({ shadowDepthSortKey, itemIndex });
                }
            }
        };

        for (MeshRenderProxy* meshRenderProxy : scene.getMeshRenderProxies())
            cullDrawable(meshRenderProxy);

        for (QuadRenderProxy* quadRenderProxy : scene.getQuadRenderProxies())
            cullDrawable(quadRenderProxy);

        for (LocalLightProxy* localLightProxy : scene.getLocalLightProxies())
        {
            if (!localLightProxy->isVisible())
                continue;

            Sphered boundingSphere(localLightProxy->getPosition(), static_cast<double>(localLightProxy->getAttenuationRadius()));
            if (frustum.contains(boundingSphere))
            {
                renderData.localLights->push_back(localLightProxy);
            }
        }

        if (!renderData.preDepthBucket->empty())
            std::sort(renderData.preDepthBucket->begin(), renderData.preDepthBucket->end());

        if (!renderData.opaqueBucket->empty())
            std::sort(renderData.opaqueBucket->begin(), renderData.opaqueBucket->end());

        if (!renderData.translucentBucket->empty())
            std::sort(renderData.translucentBucket->begin(), renderData.translucentBucket->end());

        if (!renderData.shadowDepthBucket->empty())
            std::sort(renderData.shadowDepthBucket->begin(), renderData.shadowDepthBucket->end());

        buildDrawBatches(*renderData.drawItems, *renderData.opaqueBucket, *renderData.opaqueBatches);
        buildDrawBatches(*renderData.drawItems, *renderData.shadowDepthBucket, *renderData.shadowDepthBatches);
    }

    void SceneRenderer::buildDrawBatches(const DrawItemList& drawItems, const DrawItemBucket& bucket, DrawBatchList& outBatchList)
    {
        outBatchList.batches.reserve(bucket.size());
        outBatchList.instanceIndices.reserve(bucket.size());

        const DrawItem* prevDrawItem = nullptr;
        for (const DrawItemEntry& entry : bucket)
        {
            const DrawItem& drawItem = drawItems[entry.itemIndex];

            bool canInstance = prevDrawItem &&
                (drawItem.material == prevDrawItem->material) &&
                drawItem.hasSameGeometry(*prevDrawItem) &&
                (drawItem.sectionIndex == prevDrawItem->sectionIndex);
            if (!canInstance)
            {
                outBatchList.batches.push_back(DrawBatch{ entry.itemIndex, 0, static_cast<uint32_t>(outBatchList.instanceIndices.size()) });
                prevDrawItem = &drawItem;
            }

            outBatchList.batches.back().instanceCount++;
            outBatchList.instanceIndices.push_back(drawItem.instanceIndex);
        }
    }

}
