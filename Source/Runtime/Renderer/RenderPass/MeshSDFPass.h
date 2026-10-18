#pragma once
#include "../SceneView.h"
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 测试性质的场景 SDF 可视化 Pass: 独立于 DeferredPipeline 之外(不接入 RenderPipeline.cpp
    // 的正常渲染流程, 不影响生产渲染路径), 只画 scene.getMeshRenderProxies() 里第一个带有效
    // MeshShapeProxy 的实例, 自己 clear SceneColor 后直接画完——不依赖 GBufferPass/光照管线
    // 先跑过。用于验证 MeshShapeProxy + DistanceFieldBrickAtlas 这条数据链路整体是否走得通。
    class MeshSDFPass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& view
        );
    };
}
