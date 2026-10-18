#pragma once
#include "../SceneProxy.h"
#include "../RendererContext.h"

#include <RenderBase/RenderGraph/RenderGraph.h>

namespace hyp
{
    // 每帧按距离挑出预算内的 card 重新分配页, 并只捕获这些 card 新分配的页: 按它每一张 card 的
    // 正交视角, 把材质实际颜色(lerp(baseColor, black, metallic))画进一张每帧新建的临时捕获图集,
    // 再由 CardCaptureCopyPass 拷进 SurfaceCacheAtlas 共享图集——见 Shader/Material/CardCapture.slang
    // 顶部的说明: 这条路径完全不经过 sViewData/ViewDataManager, 相机(=card)的位置朝向全部通过
    // push constant 传给 shader, 不需要给每个 card 分配 viewId。
    //
    // 一张 card 如果分辨率超过一页, 会拆成多次单独捕获, 每页各自是一次完整的正交渲染
    // (照抄 UE"CardPage 才是渲染最小单位"的设计), 因为多页在图集里不保证连续
    class CardCapturePass
    {
    public:
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene
        );
    };
}
