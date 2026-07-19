#pragma once
#include "../RendererContext.h"
#include "../SceneProxy.h"
#include "../AtmosphereProxy.h"
#include "../SkyLightProxy.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class SkyLightPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            SkyLightProxy& skyLight,
            const AtmosphereProxy* atmosphere
        );

    private:
        static void addCaptureSkyPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SkyLightProxy& skyLight,
            const AtmosphereProxy& atmosphere,
            uint32_t captureResolution,
            uint32_t captureMipCount
        );

        static void addGenSkyLightMipmapPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            uint32_t captureResolution,
            uint32_t captureMipCount
        );

        static void addSkyLightSHCoeffPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            uint32_t sourceResolution,
            rhi::TextureFormat sourceFormat,
            rg::BufferHandle packedSHCoeff
        );

        static void addReadbackPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            SkyLightProxy::CaptureRequest captureRequest,
            uint32_t prefilterResolution,
            uint32_t prefilterMipCount,
            rg::BufferHandle packedSHCoeff
        );

        static void addSkyLightPrefilterPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            uint32_t sourceResolution,
            uint32_t sourceMipCount,
            uint32_t prefilterResolution,
            uint32_t prefilterMipCount
        );
    };
}
