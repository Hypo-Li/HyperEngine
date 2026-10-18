#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"
#include "../PostProcessState.h"
#include "LumenSceneUpdatePass.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class CameraProxy;

    // 见 Document/Lumen_Reflections_Plan.md §3.2(R2)。必须在 ReflectionGenerateRaysPass 之后、
    // TAAPass 之前调用, 否则屏幕命中读到的不是 TAA history 里上一帧的颜色。lumenUpdate.ready
    // 为假时 GDF/MDF 追踪这一级整段跳过。
    class ReflectionTracePass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const CameraProxy& camera,
            PostProcessState& postProcess,
            const LumenSceneUpdateResult& lumenUpdate
        );
    };
}
