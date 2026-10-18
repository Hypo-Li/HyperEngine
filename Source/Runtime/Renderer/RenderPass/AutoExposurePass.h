#pragma once
#include "../RendererContext.h"
#include "../SceneProxy.h"
#include "../SceneView.h"
#include "../PostProcessState.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class AutoExposurePass
    {
    public:
        // Adds HistogramAtomic -> HistogramConvert -> AutoExposure and publishes the
        // PostProcessState-owned persistent AutoExposureData buffer through the render-graph
        // blackboard. Reads "SceneColorHalfRes" from the blackboard (RenderPipeline.cpp calls
        // DownsamplePass before this) instead of downsampling it here.
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            PostProcessState& postProcess
        );
    };
}
