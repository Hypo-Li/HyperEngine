#pragma once
#include "../SceneRendererContext.h"
#include "../SceneProxy.h"
#include "../SceneView.h"
#include "../AtmosphereProxy.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class AtmospherePass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );

    private:
        static void addTransmittanceLutPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneProxy& scene,
            const AtmosphereProxy& atmosphere
        );

        static void addMultiScatteringLutPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneProxy& scene,
            const AtmosphereProxy& atmosphere
        );

        static void addSkyViewLutPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );

        static void addAerialPerspectiveLutPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );

        static void addAtmospherePass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const AtmosphereProxy& atmosphere
        );

        static vk::Pipeline* createAtmospherePipeline(
            SceneRendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const vk::DescriptorSetLayout& descriptorSetLayout
        );
    };
}
