#pragma once
#include "RendererContext.h"
#include "SceneProxy.h"

namespace hyp
{
    class SceneRenderer
    {
    public:
        SceneRenderer(RendererContext& rendererContext);

        void render(SceneProxy& scene, std::span<const SceneViewGroup> sceneViewGroups, rg::RenderGraph& renderGraph);

    private:
        static uint64_t makeOpaqueSortKey(
            uint32_t materialId,
            uint32_t vertexLayoutId,
            uint32_t geometryId,
            uint32_t sectionIndex,
            float distance,
            float maxDistance
        );

        static uint64_t makeTranslucentSortKey(
            int8_t sortOrder,
            float distance,
            uint32_t materialId,
            uint32_t geometryId
        );

        static uint64_t makeDepthOnlySortKey(
            BlendMode blendMode,
            bool hasVertexPositionOffset,
            VertexLayoutType vertexLayoutType,
            uint32_t materialId,
            uint32_t vertexLayoutId,
            uint32_t geometryId,
            uint32_t sectionIndex,
            float distance,
            float maxDistance
        );

        static void buildDrawBatches(const DrawItemList& drawItems, const DrawItemBucket& bucket, DrawBatchList& outBatchList);

        void cull(const SceneProxy& scene, const SceneView& sceneView, RenderData& renderData);

    private:
        RendererContext& mRendererContext;
    };
}
