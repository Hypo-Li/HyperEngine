#pragma once
#include "../SceneView.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 见 Document/Lumen_ScreenProbeGather_Plan.md §6.10
    class ScreenProbeShortRangeAOPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view
        );
    };
}
