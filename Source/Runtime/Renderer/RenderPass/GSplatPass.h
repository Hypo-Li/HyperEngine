#pragma once
#include "../SceneRendererContext.h"
#include "../SceneProxy.h"
#include "../SceneView.h"
#include "../GSplatScene.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class GSplatPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

    private:
        static void addGSplatCullingPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

        static void addGSplatSortPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

        static void addGSplatRenderPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

        static void addGSplatDepthPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

        static void addGSplatRelightingPass(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

        static vk::Pipeline* createGSplatRenderPipeline(
            SceneRendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const vk::DescriptorSetLayout& descriptorSetLayout
        );

        static vk::Pipeline* createGSplatDepthPipeline(
            SceneRendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const vk::DescriptorSetLayout& descriptorSetLayout
        );

        static vk::Pipeline* createGSplatRelightingPipeline(
            SceneRendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const vk::DescriptorSetLayout& descriptorSetLayout
        );
    };
}
