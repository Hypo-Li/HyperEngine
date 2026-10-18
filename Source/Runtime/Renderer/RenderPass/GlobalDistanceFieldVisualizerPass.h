#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class CameraProxy;

    // Global/Mesh Distance Field 可视化: 场景开启 Lumen 时代替整条 deferred 管线, 沿相机
    // 射线求交、命中后反查表面归属物体+Surface Cache 采色——参照 MeshSDFPass 的定位, 用于
    // 验证 GDF/MDF 合成+命中查询这条数据链路整体是否走得通, 不是生产渲染路径。
    //
    // 求交方式(GDF 粗测距 or 候选物体各自原始 per-mesh SDF 精确 sphere tracing)由
    // scene.getUseMeshDistanceField() 这个跟 SceneVisualizeMode 正交的开关选择, 在同一个
    // shader 里分支, 不是两个互斥的 Pass——两条路径产出同一份 GlobalDistanceFieldResolveResult,
    // 共用后面按 visualizeMode 着色的那部分, 见 GlobalDistanceFieldVisualizer.slang。
    //
    // 内部按顺序做三件事: (a) 每帧只需要跑一次的 CardCapturePass, 用 render graph 的
    // Blackboard 防止同一帧多个相机视图各自重复捕获; (b) 这个相机自己的
    // GlobalDistanceFieldCompositePass(内部先做 GlobalDistanceField::update() 剔选候选
    // 物体, 再合成写进 GDF atlas); (c) 实际的 fullscreen 射线求交 + 采色。
    class GlobalDistanceFieldVisualizerPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view,
            const CameraProxy& camera
        );
    };
}
