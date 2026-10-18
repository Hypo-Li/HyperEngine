#pragma once
#include "../RendererContext.h"
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
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

    private:
        static void addGSplatCullingPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

        static void addGSplatSortPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

        // GSplatSort 拆成的 4(kRadixSortPassCount)*3(Upsweep/Spine/Downsweep) = 12 个独立的 RenderGraph
        // compute pass 之一。之所以拆开而不是像 addGSplatSortPass 原来那样在一个 pass 里手动插 barrier,
        // 是为了让 WebGPU 端能通过每个 pass 声明的 buffer 读写访问,自动推导出 pass 间的 barrier
        static void addRadixSortUpsweepPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            uint32_t viewId,
            uint32_t partitionCount,
            uint32_t pass,
            rg::BufferHandle gsplatDrawIndirect,
            rg::BufferHandle keysIn,
            rg::BufferHandle& globalHistogram,
            rg::BufferHandle& partitionHistogram
        );

        static void addRadixSortSpinePass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            uint32_t viewId,
            uint32_t pass,
            rg::BufferHandle gsplatDrawIndirect,
            rg::BufferHandle& globalHistogram,
            rg::BufferHandle& partitionHistogram
        );

        static void addRadixSortDownsweepPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            uint32_t viewId,
            uint32_t splatCount,
            uint32_t partitionCount,
            uint32_t pass,
            rg::BufferHandle gsplatDrawIndirect,
            rg::BufferHandle globalHistogram,
            rg::BufferHandle partitionHistogram,
            rg::BufferHandle keysIn,
            rg::BufferHandle& keysOut,
            rg::BufferHandle valuesIn,
            rg::BufferHandle& valuesOut
        );

        static void addGSplatRenderPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

        static void addGSplatDepthPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const GSplatScene& gsplatScene
        );

        static rhi::Pipeline* createGSplatRenderPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        );

        static rhi::Pipeline* createGSplatDepthPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        );
    };
}
