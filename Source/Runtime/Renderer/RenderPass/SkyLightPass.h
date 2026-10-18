#pragma once
#include "../RendererContext.h"
#include "../SceneProxy.h"
#include "../AtmosphereProxy.h"
#include "../SkyBackgroundProxy.h"
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
            const AtmosphereProxy* atmosphere,
            const SkyBackgroundProxy* skyBackground
        );

    private:
        static void addCaptureSkyPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SkyLightProxy& skyLight,
            const AtmosphereProxy* atmosphere,
            const SkyBackgroundProxy* skyBackground,
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

        // 实时捕获模式用: 只把这一帧算好的瞬时 prefilter 结果 resolve 进常驻纹理, 不做 CPU 回读、
        // 不产出 SkyLightCaptureResult——目标纹理直接原地覆盖, 不像 addReadbackPass 那样可能
        // 指向一个待 swap 的新纹理
        static void addResolvePrefilterPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            TextureProxy* specularPrefilterTarget,
            uint32_t prefilterResolution,
            uint32_t prefilterMipCount
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
