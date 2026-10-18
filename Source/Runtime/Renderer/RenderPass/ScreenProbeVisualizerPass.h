#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 屏幕探针的临时调试可视化: 读 ScreenProbePlacementPass(和 ScreenProbeTracePass)已经产出
    // 的探针数据, 用一次 instanced draw 给每个探针画一个 3x3 十字标记, 叠在正常光照结果上,
    // 见 Document/Lumen_ScreenProbeGather_Plan.md M2/M3、Shader/ScreenProbe/ScreenProbeVisualizer.slang。
    // ScreenProbePlacement 模式统一探针绿色、自适应探针红色, 验证自适应探针是不是贴着深度/法线
    // 突变边缘分布; ScreenProbeRadiance 模式按这个探针 64 条追踪光线的平均辐射度染色。挂载方式
    // 跟 ScreenTraceRadiance 一样: 等正常延迟管线(含 TAA/ColorGrading)跑完之后作为最后一笔叠加,
    // 不是替换整条管线; 布点和追踪本身在 TAA 之前跑, 见 RenderPipeline.cpp。
    class ScreenProbeVisualizerPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view
        );
    };
}
