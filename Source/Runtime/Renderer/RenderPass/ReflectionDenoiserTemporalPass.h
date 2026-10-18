#pragma once
#include "../SceneView.h"
#include "../RendererContext.h"
#include "../PostProcessState.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 见 Document/Lumen_Reflections_Plan.md §3.4(R4)。必须在 ReflectionResolvePass 之后、
    // TAAPass 之前调用(表面重投影读的是上一帧场景深度/TAA 无关, 但跟 ScreenProbeTemporalFilterPass
    // 一样排在正常时序里, 不额外要求 TAA history)。
    class ReflectionDenoiserTemporalPass
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
