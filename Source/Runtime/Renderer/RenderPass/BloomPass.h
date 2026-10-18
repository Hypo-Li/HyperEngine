#pragma once
#include "../RendererContext.h"
#include "../SceneView.h"
#include "../PostProcessState.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // SIGGRAPH 2014 COD:AW 高质量 Bloom(Jorge Jimenez): 从 blackboard 读 "SceneColorHalfRes"
    // (RenderPipeline.cpp 里 DownsamplePass 已经算好, 这里不重复降采样), 在一张多 mip 的
    // BloomPyramid 纹理上做降采样金字塔 + tent 升采样累加, 最终把半分辨率结果发布成 blackboard
    // 里的 "BloomTexture", 交给 ColorGradingPass 采样合成——不做单独的 composite pass。
    // 和 DownsamplePass/AutoExposurePass 之间没有任何直接 C++ 依赖, 只通过 blackboard 通信。
    class BloomPass
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
