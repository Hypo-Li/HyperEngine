#pragma once
#include "../SceneView.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // OutputRouting::Copy_Resolve implementation. Present is first evaluated
    // into a transient texture; an isolated copy pass then writes the imported
    // render target without binding the renderer's global texture tables.
    class CopyToRenderTargetPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view
        );

    private:
        static rhi::Pipeline* createResolvePipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        );

        static rhi::Pipeline* createCopyPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout,
            rhi::TextureFormat dstFormat
        );
    };
}
