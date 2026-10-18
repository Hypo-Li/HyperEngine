#pragma once
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class CameraProxy;

    // Surface Cache 间接光(Radiosity): [A] Trace(半球探针追 GDF, 命中采上一帧 FinalLighting、
    // 未命中采天光)-> [B] SpatialFilter(探针级 4 邻域空间滤波)-> [C] ConvertToSH(每探针 16
    // 采样投影成 1 阶球谐)-> [D] Integrate(双线性插值 4 个探针 SH, 跟 texel 自己的法线做余弦
    // 卷积点积得到辐照度, 再跟历史 IndirectLightingAtlas 按 alpha=1/(1+N) 时域混合), 结果写进
    // SurfaceCacheAtlas::IndirectLightingAtlas, 见 Document/Lumen_Radiosity_Plan.md M4。
    //
    // 固定用主相机的 GDF(点/聚光阴影、CardDirectLighting 也是同一个约定, 见
    // Lumen_GI_Roadmap.md 对 UE PrimaryView 用法的调研结论)——调用方
    // (GlobalDistanceFieldVisualizerPass)只应该在处理主相机视图时调用这个 pass, 且必须排在
    // CardDirectLightingPass 之后(Trace 要采它写的 DirectLighting 参与合成的上一帧
    // FinalLighting)、CombineFinalLightingPass 之前(Combine 要把这个 pass 写的 Indirect 项
    // 加进去)。
    class RadiosityPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const CameraProxy& cameraProxy
        );

    private:
        static void addTracePass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const CameraProxy& cameraProxy
        );

        static void addFilterPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const CameraProxy& cameraProxy
        );

        static void addConvertToSHPass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const CameraProxy& cameraProxy
        );

        static void addIntegratePass(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const CameraProxy& cameraProxy
        );
    };
}
