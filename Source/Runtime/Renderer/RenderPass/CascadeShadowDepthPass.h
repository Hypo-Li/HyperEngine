#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RenderData.h"
#include "../RendererContext.h"
#include "../DirectionalLightProxy.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class CascadeShadowDepthPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            std::span<const SceneView> views,
            std::span<const RenderData> renderData,
            const DirectionalLightProxy& directionalLight
        );

    private:
        static rhi::Pipeline* createPipeline(
            RendererContext& rendererContext,
            MaterialProxy& material,
            HString renderTag,
            VertexLayout vertexLayout
        );
    };
}
