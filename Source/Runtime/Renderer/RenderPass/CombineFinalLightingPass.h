#pragma once
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // Surface Cache 最终光照合成: 对已捕获的每个 Card 页, 逐 texel 算
    // FinalLighting = max(0, (DirectLighting + IndirectLighting)·Albedo/π + Emissive), 写进
    // SurfaceCacheAtlas::FinalLightingAtlas。M1 阶段 IndirectLightingAtlas 还不存在, 恒为 0。
    // 不带任何 view/相机概念——纯粹是图集到图集的逐 texel 公式, 见
    // Shader/Material/CombineFinalLighting.slang。调用方(GlobalDistanceFieldVisualizerPass)
    // 必须保证这个 pass 排在 CardDirectLightingPass 之后, 见 Document/Lumen_Radiosity_Plan.md M1。
    class CombineFinalLightingPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene
        );
    };
}
