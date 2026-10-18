#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"
#include "../AtmosphereProxy.h"
#include "../CameraProxy.h"
#include "../RenderData.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class DirectLightingPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const RenderData& renderData,
            const AtmosphereProxy* atmosphere,
            const CameraProxy& camera
        );

    private:
        static rhi::Pipeline* createPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout,
            bool hasLightVolumeDepth
        );

        static rhi::Pipeline* createLightVolumePipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        );
    };
}
