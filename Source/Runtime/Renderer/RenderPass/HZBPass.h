#pragma once
#include "../RendererContext.h"
#include "../SceneView.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // Builds a reusable reversed-Z depth hierarchy from SceneDepthD, R=Furthest(min)/
    // G=Closest(max) per texel. Mip 0 is half resolution; mip count is computed per frame
    // from the viewport size(getMipCount, 跟 UE SceneTextureReductions.cpp 的
    // floor(log2(max(W,H))) 同一个公式, 降到接近 1x1 为止, 不再固定层数), 全部 mip 通过
    // "SceneHZB" blackboard key 发布。
    class HZBPass
    {
    public:
        static uint2 getBaseExtent(uint2 sceneExtent);

        // baseExtent 是 getBaseExtent() 的返回值(mip0 尺寸), 不是原始 sceneExtent——
        // 调用方(AOPass/屏幕空间追踪)必须传同一个 baseExtent 才能跟这个 pass 实际构建出的
        // mip 数量保持一致
        static uint32_t getMipCount(uint2 baseExtent);

        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view
        );
    };
}
