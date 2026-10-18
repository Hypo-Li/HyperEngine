#pragma once
#include "../SceneView.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // Presents SceneColor directly into an imported render target. Unlike
    // PresentPass, this is a regular graphics pass rather than a swapchain pass.
    class PresentToRenderTargetPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view
        );

    private:
        static rhi::Pipeline* createPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout,
            rhi::TextureFormat dstFormat
        );
    };
}
