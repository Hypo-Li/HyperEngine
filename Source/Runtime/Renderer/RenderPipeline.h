#pragma once
#include <vector>

#include "SceneProxy.h"
#include "SceneView.h"
#include "RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class DeferredPipeline
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const RenderData& renderData
        );
    };

    class CascadeShadowPipeline
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
    };
}
