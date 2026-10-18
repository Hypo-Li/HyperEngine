#pragma once
#include "../RendererContext.h"

#include <Core/Math/Vector.h>
#include <RenderBase/RenderGraph/RenderGraph.h>

#include <vector>

namespace hyp
{
    class SurfaceCacheAtlas;

    struct CardCaptureCopyTask
    {
        int2 captureOrigin;
        int2 atlasOrigin;
        int2 extent;
    };

    struct CardCaptureTextures
    {
        rg::TextureHandle albedo;
        rg::TextureHandle normal;
        rg::TextureHandle emissive;
        rg::TextureHandle depth;
    };

    // 把临时捕获图集里的页拷进 SurfaceCacheAtlas 物理图集的 albedo/normal/emissive/depth 四层
    class CardCaptureCopyPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            SurfaceCacheAtlas& surfaceCacheAtlas,
            const CardCaptureTextures& captureTextures,
            std::vector<CardCaptureCopyTask>&& tasks
        );
    };
}
