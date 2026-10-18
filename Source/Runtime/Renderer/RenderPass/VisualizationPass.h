#pragma once
#include "../SceneView.h"
#include "../RendererContext.h"
#include "../VisualizationItems.h"
#include "../VisualizationRenderData.h"
#include "../CameraProxy.h"
#include "../SceneProxy.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class VisualizationPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const CameraProxy& camera,
            VisualizationItems&& items
        );
    };
}
