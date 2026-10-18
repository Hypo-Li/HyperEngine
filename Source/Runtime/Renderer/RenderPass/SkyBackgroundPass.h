#pragma once
#include "../RendererContext.h"
#include "../SceneProxy.h"
#include "../SceneView.h"
#include "../SkyBackgroundProxy.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class SkyBackgroundPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const SkyBackgroundProxy& skyBackground
        );

        static void addCaptureToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SkyBackgroundProxy& skyBackground,
            HString cubemapName,
            const rhi::TextureDesc& cubemapDesc
        );
    };
}
