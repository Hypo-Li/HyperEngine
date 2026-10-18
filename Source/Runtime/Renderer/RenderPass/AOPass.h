#pragma once
#include "../RendererContext.h"
#include "../SceneView.h"
#include "../PostProcessState.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 屏幕空间环境光遮蔽的入口, 按 AmbientOcclusionSettings::method 分流到具体实现:
    // - GTAO(addGTAOToRenderGraph, AOPass.cpp): Generate -> SpatialFilter -> TemporalFilter,
    //   时域历史存在 PostProcessState::mAOHistory(跟 TAA 的 mTAAHistory 是两码事), 见
    //   PostProcessState.h 里 ensureAOHistory() 一组方法的注释。
    // - SSAO(addSSAOToRenderGraph, AOPass.cpp): Setup -> 半分辨率 AO -> 全分辨率 AO,
    //   严格对照 UE PostProcessAmbientOcclusion.usf 重写, 见 Shader/PostProcess/SSAOCommon.slang。
    // 两条路径殊途同归, 都把收敛好的结果发布进 blackboard 的 "AmbientOcclusion", 交给
    // IndirectLightingPass 消费。
    class AOPass
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
