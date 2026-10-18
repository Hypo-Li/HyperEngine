#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // Screen Probe Gather 的探针网格布点: 统一网格降采样(固定像素间距 + 共享时域抖动)+
    // 自适应探针分配(按插值权重覆盖不足自动补点), 见 Document/Lumen_ScreenProbeGather_Plan.md
    // M2、Shader/ScreenProbe/ScreenProbeCommon.slang 顶部的算法/偏差说明。只产出探针自身的
    // 深度/世界空间法线/世界空间速度图集、自适应探针屏幕坐标 buffer 和按实际探针数算好的
    // 间接 dispatch 参数, 不追踪、不采光——追踪(M3)接在这之后, 消费这里发布到 blackboard 的句柄。
    class ScreenProbePlacementPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view
        );

        // ceil(viewExtent / kScreenProbeDownsampleFactor)——跟 Shader/ScreenProbe/
        // ScreenProbeCommon.slang 里的 kScreenProbeDownsampleFactor 必须保持一致(见 .cpp
        // 顶部注释), 调用方(未来的追踪 pass)需要同一份网格尺寸来对齐图集坐标
        static uint2 getTileGridSize(uint2 viewExtent);

        // 统一探针数量(tileGridSize.x*y)按分配比例换算出的自适应探针预算, 以及"统一探针图集
        // 再往下追加自适应探针所需行数"之后的完整图集尺寸——跟 UE
        // ScreenProbeAtlasBufferSize(LumenScreenProbeGather.cpp)同一个做法, 统一探针/自适应
        // 探针共用一张图集, 不是两张独立纹理, 见 .cpp 顶部注释
        static uint32_t getMaxNumAdaptiveProbes(uint2 tileGridSize);
        static uint2 getAtlasViewSize(uint2 tileGridSize);
    };
}
