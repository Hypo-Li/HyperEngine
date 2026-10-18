#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 屏幕空间 HZB 追踪(Shader/Common/HZBTrace.slang)的临时调试可视化: 每像素发一条视线绕
    // 法线的镜面反射光线, 对这一帧已经光栅化出来的真实屏幕内容(GBuffer+HZB)求交, 用来验证
    // HZBTrace.slang 本身的正确性——不属于 Surface Cache/GDF 那一套, 不接在
    // GlobalDistanceFieldVisualizerPass 里, 也不是 Screen Probe Gather 正式管线的一部分,
    // 完成阶段性验证后应当删除。两个模式(SceneVisualizeMode::ScreenTraceHitState/
    // ScreenTraceRadiance)在 RenderPipeline.cpp 里挂的位置不一样: 前者不需要真实光照数据,
    // 全替换这一帧剩余的光照/后处理链路; 后者命中要采一个真实存在的画面颜色, 必须等正常
    // 光照链路(含 TAA)跑完之后, 在最后一步才把 "SceneColor" 换成这个 pass 的输出——见
    // RenderPipeline.cpp 的三段式分派
    class ScreenTraceVisualizerPass
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
