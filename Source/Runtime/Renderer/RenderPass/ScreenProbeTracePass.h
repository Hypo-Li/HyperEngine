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

    // 必须在 ScreenProbePlacementPass 之后、TAAPass 之前调用, 否则屏幕命中读到的不是 TAA history 里上一帧的颜色。
    // lumenUpdate.ready 为假时 GDF 追踪这一级整段跳过。
    class ScreenProbeTracePass
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

        // 把这一帧的深度拷进 SceneDepthHistory, 留给下一帧当历史用——必须排在这一帧所有读它的 pass
        // (这个类自己的 Trace pass、ScreenProbeTemporalFilterPass)之后调用, 不能紧跟在 addToRenderGraph 后面
        static void addSceneDepthHistoryCopyToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view,
            PostProcessState& postProcess
        );
    };
}
