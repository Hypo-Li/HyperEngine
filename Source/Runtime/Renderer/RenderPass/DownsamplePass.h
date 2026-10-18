#pragma once
#include "../RendererContext.h"
#include "../SceneView.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 通用工具 pass: 把 blackboard 里的 "SceneColor" 降采样到半分辨率, 发布成
    // "SceneColorHalfRes" 存回 blackboard——没有返回值, 调用方(RenderPipeline.cpp)不持有、
    // 也不需要持有这个 handle, 后面谁要用(AutoExposurePass 给直方图统计降本、BloomPass 当
    // 金字塔起点)自己从 blackboard 里取, 这个 pass 和它们之间没有直接的 C++ 依赖关系。
    //
    // 目前 RenderPipeline.cpp 只会调用一次, 但这里仍然做了防御性处理: 如果 blackboard 里
    // "SceneColorHalfRes" 已经存在(以后如果哪天出现多个调用方各自调一次的情况), 直接复用
    // 已经建好的那张纹理, 不会重复创建资源。
    class DownsamplePass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view
        );
    };
}
