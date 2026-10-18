#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"
#include "../PostProcessState.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 必须在 ScreenProbePlacementPass 之后调用。对应 UE LumenScreenProbeImportanceSampling.cpp,
    // 见 Document/Lumen_ScreenProbeGather_Plan.md §9。M7.1-M7.4 均已实现
    class ScreenProbeImportanceSamplingPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            PostProcessState& postProcess
        );

        // 把这一帧空间滤波后的探针辐射度/深度拷进 Lighting PDF 的历史资源, 留给下一帧用——必须
        // 排在 ScreenProbeFilteringPass(产出 ScreenProbeRadianceFiltered)之后调用, 不能紧跟在
        // addToRenderGraph 后面, 见 ScreenProbeTracePass::addSceneDepthHistoryCopyToRenderGraph
        // 同一个理由
        static void addLightingHistoryCopyToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view,
            PostProcessState& postProcess
        );
    };
}
