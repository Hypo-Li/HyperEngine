#pragma once
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class CameraProxy;
    class AtmosphereProxy;

    // Surface Cache 直接光照: 对 CardCapturePass 已经捕获好的每个 Card 页, 按 texel 反解出
    // 世界坐标+法线, 遍历场景全部光源算 Lambertian 漫反射, 方向光阴影追 per-mesh MDF
    // (LumenScene::DirectLightingShadowTileGrid 筛出的候选), 点光源/聚光灯阴影追 cameraProxy
    // 这一个相机(固定传主相机, 见调用方 GlobalDistanceFieldVisualizerPass::addToRenderGraph
    // 的注释)合成好的 GDF, 结果写进 SurfaceCacheAtlas::DirectLightingAtlas。方向光额外按
    // atmosphere 穿过大气的透过率衰减(atmosphere 为空时跳过, 等价于透过率恒为 1), 跟
    // DirectLightingPass 同一套 TransmittanceLut 用法。见
    // Shader/Material/CardDirectLighting.slang 顶部的说明。
    //
    // 独立于 DeferredPipeline 之外(不接入 RenderPipeline.cpp 的正常渲染流程), 跟
    // CardCapturePass/MeshSDFPass 同样的理由: 先把这条数据链路本身实现对、验证过(挂在
    // GlobalDistanceFieldVisualizerPass 里可视化), 接入实时管线是分开的下一步。
    class CardDirectLightingPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const CameraProxy& cameraProxy,
            const AtmosphereProxy* atmosphere
        );
    };
}
