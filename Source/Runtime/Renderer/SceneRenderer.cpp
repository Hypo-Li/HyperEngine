#include "SceneRenderer.h"

namespace hyp
{
    SceneRenderer::SceneRenderer(SceneRendererContext& sceneRendererContext) :
        mSceneRendererContext(sceneRendererContext)
    {
    }

    struct UploadData
    {
        SceneProxy* scene;
        std::vector<MaterialProxy*> materials;
        std::vector<GeometryProxy*> geometries;
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

        if (sceneViewCount > 4)
        {
            auto range = std::views::iota(0ull, sceneViewCount);
            // NOTE: GCC 需要链接 Intel TBB
            std::for_each(std::execution::par, range.begin(), range.end(),
                [&](size_t i)
                {
                    cull(scene, *sceneViewPtrs[i], renderData[i]);
                }
            );
        }
        else
        {
            for (size_t i = 0; i < sceneViewPtrs.size(); i++)
            {
                cull(scene, *sceneViewPtrs[i], renderData[i]);
            }
        }

        renderGraph.addComputePass<UploadData>(
            "Upload"_hs,
            [&](rg::RenderGraph::Builder& builder, UploadData& data)
            {
                builder.markSideEffect();

                data.scene = &scene;
                for (auto& rd : renderData)
                {
                    MaterialProxy* prevMaterial = nullptr;
                    GeometryProxy* prevGeometry = nullptr;
                    for (auto& sortedDrawItem : *rd.opaqueBucket)
                    {
                        auto& drawItem = sortedDrawItem.drawItem;
                        if (prevMaterial != drawItem.material)
                        {
                            data.materials.push_back(drawItem.material);
                            prevMaterial = drawItem.material;
                        }
                        if (prevGeometry != drawItem.geometry)
                        {
                            data.geometries.push_back(drawItem.geometry);
                            prevGeometry = drawItem.geometry;
                        }
                    }

                    for (auto& sortedDrawItem : *rd.translucentBucket)
                    {
                        auto& drawItem = sortedDrawItem.drawItem;
                        // Translucent Bucket 不是严格按材质排序的,
                        // 因此无法利用 prevMaterial 减少 prepare 调用;
                        // 但是透明材质较少, 这些开销是可以接受的
                        data.materials.push_back(drawItem.material);
                        data.geometries.push_back(drawItem.geometry);
                    }
                }

                std::sort(data.materials.begin(), data.materials.end());
                std::sort(data.geometries.begin(), data.geometries.end());

                data.viewDataManager = &mSceneRendererContext.getViewDataManager();
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
                auto& cmd = context.getCommandBuffer();
                data.scene->updateSceneData(cmd);

                MaterialProxy* prevMaterial = nullptr;
                for (MaterialProxy* material : data.materials)
                {
                    if (prevMaterial != material)
                    {
                        material->prepare(cmd);
                        prevMaterial = material;
                    }
                }

                GeometryProxy* prevGeometry = nullptr;
                for (GeometryProxy* geometry : data.geometries)
                {
                    if (prevGeometry != geometry)
                    {
                        geometry->prepare(cmd);
                        prevGeometry = geometry;
                    }
                }

                data.viewDataManager->updateViewData(cmd, data.viewIds, data.viewDatas);
            }
        );

        size_t renderDataIndex = 0;
        for (auto& sceneViewGroup : sceneViewGroups)
        {
            switch (sceneViewGroup.getType())
            {
            case SceneViewGroup::Type::Camera:
            {
                DeferredPipeline::addToRenderGraph(
                    renderGraph,
                    mSceneRendererContext,
                    scene,
                    sceneViewGroup.getSceneViews()[0],
                    renderData[renderDataIndex]
                );
                ++renderDataIndex;
                break;
            }
            case SceneViewGroup::Type::Cascade_Shadow:
            {
                auto sceneViews = sceneViewGroup.getSceneViews();
                auto directionalLight = sceneViewGroup.getSource<DirectionalLightProxy>();
                CascadeShadowPipeline::addToRenderGraph(
                    renderGraph,
                    mSceneRendererContext,
                    scene,
                    sceneViews,
                    { &renderData[renderDataIndex], sceneViews.size() },
                    *directionalLight
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
            vertexLayoutType == VertexLayoutType::Static_Mesh);
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
        auto& renderTagRegistry = mSceneRendererContext.getRenderBackend().getRenderTagRegistry();
        uint32_t viewMask = sceneView.getRenderMask();
        const Frustumd& frustum = sceneView.getFrustum();
        for (MeshRenderProxy* meshRenderProxy : scene.getMeshRenderProxies())
        {
            uint32_t instanceIndex = meshRenderProxy->getInstanceIndex();
            uint32_t layerMask = meshRenderProxy->getLayerMask();
            if (!meshRenderProxy->isVisible() || !(sceneView.getCullingMask() & layerMask))
                continue;

            if (!frustum.contains(meshRenderProxy->getBoundingSphere()))
                continue;

            if (!frustum.contains(meshRenderProxy->getBoundingBox()))
                continue;

            uint32_t meshMask = meshRenderProxy->getRenderMask();
            int8_t sortOrder = meshRenderProxy->getSortOrder();

            double3 meshWorldPos = meshRenderProxy->getBoundingSphere().getCenter();
            double3 cameraWorldPos = sceneView.getCameraOrigin();
            double3 distanceVec = meshWorldPos - cameraWorldPos;
            float distanceSqr = static_cast<float>(glm::dot(distanceVec, distanceVec));
            // Drawable 内部根据 View 在内部查询合适的 LOD, 并返回对应的 DrawItem
            for (const auto& drawItem : meshRenderProxy->getDrawItems(sceneView))
            {
                uint32_t materialMask = drawItem.material->getRenderMask();
                uint32_t combinedMask = (materialMask & meshMask) & viewMask;
                if (!combinedMask)
                    continue;

                BlendMode blendMode = drawItem.material->getAttributes().blendMode;
                uint32_t materialId = drawItem.material->getId();
                uint32_t vertexLayoutId = drawItem.geometry->getVertexLayout().getId();
                VertexLayoutType vertexLayoutType = drawItem.geometry->getVertexLayout().getType();
                uint32_t geometryId = drawItem.geometry->getId();
                uint32_t sectionIndex = drawItem.sectionIndex;

                static constexpr float kMaxDistanceSqr = 1000.0f * 1000.0f;

                if (combinedMask & renderTagRegistry.getBuiltinDepthOnlyMask())
                {
                    uint64_t preDepthSortKey = makeDepthOnlySortKey(blendMode, vertexLayoutType, materialId, vertexLayoutId, geometryId, sectionIndex, distanceSqr, kMaxDistanceSqr);
                    renderData.preDepthBucket->emplace_back(preDepthSortKey, drawItem);
                }

                if (combinedMask & renderTagRegistry.getBuiltinOpaqueMask())
                {
                    uint64_t opaqueSortKey = makeOpaqueSortKey(materialId, vertexLayoutId, geometryId, sectionIndex, distanceSqr, kMaxDistanceSqr);
                    renderData.opaqueBucket->emplace_back(opaqueSortKey, drawItem);
                }
                else if (combinedMask & renderTagRegistry.getBuiltinTranslucentMask())
                {
                    uint64_t translucentSortKey = makeTranslucentSortKey(sortOrder, distanceSqr, materialId, geometryId);
                    renderData.translucentBucket->emplace_back(translucentSortKey, drawItem);
                }

                if (combinedMask & renderTagRegistry.getBuiltinShadowDepthMask())
                {
                    uint64_t shadowDepthSortKey = makeDepthOnlySortKey(blendMode, vertexLayoutType, materialId, vertexLayoutId, geometryId, sectionIndex, distanceSqr, kMaxDistanceSqr);
                    renderData.shadowDepthBucket->emplace_back(shadowDepthSortKey, drawItem);
                }
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
    }

}
