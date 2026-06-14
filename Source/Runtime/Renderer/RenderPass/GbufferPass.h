#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RenderData.h"
#include "../SceneRendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class GBufferPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            SceneRendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const RenderData& renderData
        );

    private:
        static vk::Pipeline* createPipeline(
            SceneRendererContext& rendererContext,
            MaterialProxy& material,
            HString renderTag,
            VertexLayout vertexLayout
        );
    };
}
