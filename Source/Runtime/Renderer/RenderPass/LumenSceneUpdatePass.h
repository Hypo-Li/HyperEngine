#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class CameraProxy;

    struct LumenSceneUpdateResult
    {
        // false: 没有 LumenScene, 或者 GDF/Surface Cache 数据这一帧还没就绪, 下面的句柄不能用
        bool ready = false;
        rg::TextureHandle cardFinalLightingAtlas;
        rg::TextureHandle cardIndirectLightingAtlas;
        rg::TextureHandle cardDirectLightingAtlas;
        rg::TextureHandle cardAlbedoAtlas;
        rg::TextureHandle cardNormalAtlas;
        rg::TextureHandle cardEmissiveAtlas;
        rg::TextureHandle cardDepthAtlas;
        rg::BufferHandle cardPages;
    };

    // Lumen 场景每帧的更新链, 从 GlobalDistanceFieldVisualizerPass 抽出来供屏幕探针追踪共用。
    class LumenSceneUpdatePass
    {
    public:
        static LumenSceneUpdateResult addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const CameraProxy& camera,
            bool requireMeshDistanceFieldAtlas
        );
    };
}
