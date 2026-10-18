#pragma once
#include "CardCaptureCopyPass.h"

namespace hyp
{
    // 重置新捕获页的光照状态: Direct/Indirect 清零, Final 取自发光, Radiosity 累积帧计数清零
    class CardLightingResetPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            SurfaceCacheAtlas& surfaceCacheAtlas,
            rg::TextureHandle captureEmissive,
            std::vector<CardCaptureCopyTask>&& tasks
        );
    };
}
