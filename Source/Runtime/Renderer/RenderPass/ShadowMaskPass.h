#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"
#include "../CameraProxy.h"
#include "../DirectionalLightProxy.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 读 GBuffer 场景深度 + CascadeShadowDepthPass 产出的阴影深度 atlas, 生成一张标注屏幕空间
    // 阴影的 R8_UNORM 纹理(1=见光, 0=阴影)。最简版: 无 depthBias/slopeBias/PCF, 单点深度比较。
    // 只生成这张纹理, 不在这一步把它接进 DirectLightingPass 的实际光照计算。
    class ShadowMaskPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const CameraProxy& camera,
            const DirectionalLightProxy& directionalLight
        );

    private:
        static rhi::Pipeline* createPipeline(
            RendererContext& rendererContext,
            ShaderVariant shaderVariant,
            const rhi::BindGroupLayout& bindGroupLayout
        );
    };
}
