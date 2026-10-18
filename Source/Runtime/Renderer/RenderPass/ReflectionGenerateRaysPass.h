#pragma once
#include "../SceneView.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 见 Document/Lumen_Reflections_Plan.md §3.1(R1)
    class ReflectionGenerateRaysPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view
        );
    };
}
