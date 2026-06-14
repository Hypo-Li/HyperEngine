#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../SceneRendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class DirectLightingPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view
        );

    private:
        static vk::Pipeline* createPipeline(
            SceneRendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const vk::DescriptorSetLayout& descriptorSetLayout
        );
    };
}
