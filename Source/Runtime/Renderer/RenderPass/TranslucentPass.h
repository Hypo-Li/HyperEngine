#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../SkyLightProxy.h"
#include "../AtmosphereProxy.h"
#include "../RenderData.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class TranslucentPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const RenderData& renderData,
            SkyLightProxy* activeSkyLight,
            const AtmosphereProxy* atmosphere
        );

    private:
        static rhi::Pipeline* createPipeline(
            RendererContext& rendererContext,
            MaterialProxy& material,
            HString renderTag,
            VertexLayout vertexLayout,
            const rhi::BindGroupLayout& viewLayout
        );
    };
}
