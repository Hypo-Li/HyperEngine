#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RenderData.h"
#include "../SceneRendererContext.h"
#include "../DirectionalLightProxy.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class CascadeShadowDepthPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneProxy& scene,
            std::span<const SceneView> views,
            std::span<const RenderData> renderData,
            const DirectionalLightProxy& directionalLight
        );

    private:
        static vk::Pipeline* createPipeline(
            SceneRendererContext& rendererContext,
            MaterialProxy& material,
            HString renderTag,
            VertexLayout vertexLayout
        );
    };
}
