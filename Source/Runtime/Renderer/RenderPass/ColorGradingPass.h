#pragma once
#include "../RendererContext.h"
#include "../SceneView.h"
#include "../PostProcessState.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class ColorGradingPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view,
            PostProcessState& postProcess
        );

    private:
        // 只在 postProcess.isColorGradingLutDirty() 时被调用: 对持久 LUT 纹理发一次写入, 返回写入后
        // 的 handle(仍是同一张纹理, render graph 内部版本号前进了一步), 供 apply pass 采样
        static rg::TextureHandle addColorGradingLutPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            rg::TextureHandle lut,
            PostProcessState& postProcess
        );

        static void addColorGradingApplyPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view,
            rg::TextureHandle lut,
            PostProcessState& postProcess
        );

        static rhi::Pipeline* createColorGradingApplyPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        );
    };
}
