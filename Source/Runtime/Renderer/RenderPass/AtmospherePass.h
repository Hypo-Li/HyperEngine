#pragma once
#include "../RendererContext.h"
#include "../SceneProxy.h"
#include "../SceneView.h"
#include "../AtmosphereProxy.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class AtmosphereLutPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );

    private:
        static void addTransmittanceLutPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const AtmosphereProxy& atmosphere
        );

        static void addMultiScatteringLutPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const AtmosphereProxy& atmosphere
        );

        static void addSkyViewLutPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );

        static void addAerialPerspectiveLutPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );
    };

    class AtmospherePass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );

    private:
        static rhi::Pipeline* createAtmospherePipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        );
    };
}
