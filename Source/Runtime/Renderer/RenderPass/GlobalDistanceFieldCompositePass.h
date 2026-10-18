#pragma once
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    class CameraProxy;

    // Global Distance Field 合成: 先跑 GlobalDistanceField::update()(第五步)剔选候选
    // 物体, 再逐个待合成的 cascade/层各自 dispatch 一次 compute, 合成写进这个相机自己的
    // GDF brick atlas, 见 GlobalDistanceFieldComposite.slang。
    //
    // 独立于 DeferredPipeline 之外(不接入 RenderPipeline.cpp 的正常渲染流程), 跟
    // MeshSDFPass 同样的理由: 先把这条数据链路本身实现对、验证过, 接入实时管线是分开的
    // 下一步。
    class GlobalDistanceFieldCompositePass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const CameraProxy& cameraProxy
        );
    };
}
