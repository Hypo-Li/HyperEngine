#pragma once
#include "../RendererContext.h"
#include "../SceneView.h"
#include "../PostProcessState.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 时域抗锯齿 resolve pass: 把 CameraProxy 抖动过的这一帧 SceneColor 和常驻的上一帧 history
    // 混合, 输出写回 blackboard 的 "SceneColor"(后面 ColorGradingPass 直接消费, 不需要感知这里
    // 发生过什么), 同时这份输出就是下一帧的 history——见 PostProcessState::getTAAHistoryWriteView()。
    // 只应该在 postProcess.isJitterEnabled() 为真时插入(未开 TAA 的相机不产生抖动, 也没有意义去做
    // 时域累积), 调用方在 RenderPipeline.cpp 里按这个开关判断要不要插这个 pass。
    class TAAPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view,
            PostProcessState& postProcess
        );

    private:
        static rhi::Pipeline* createPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        );
    };
}
