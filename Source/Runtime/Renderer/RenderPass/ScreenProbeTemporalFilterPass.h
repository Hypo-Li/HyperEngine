#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"
#include "../PostProcessState.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 必须在 ScreenProbeIntegratePass 之后、ScreenProbeDiffuseIndirectCompositePass 之前调用。
    class ScreenProbeTemporalFilterPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view,
            PostProcessState& postProcess
        );
    };
}
