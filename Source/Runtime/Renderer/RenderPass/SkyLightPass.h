#pragma once
#include "../SceneRendererContext.h"
#include "../SceneProxy.h"
#include "../SceneView.h"
#include "../AtmosphereProxy.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class SkyLightPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& context,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );

    private:
        static void addCaptureSkyPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& context,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );

        static void addGenSkyLightMipmapPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& context,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );
    };
}
