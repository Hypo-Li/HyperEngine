#pragma once
#include <Renderer/SceneView.h>
#include <Renderer/SceneProxy.h>
#include <Renderer/CameraProxy.h>
#include <Renderer/RendererContext.h>

#include <RenderBase/RenderGraph/RenderGraph.h>
#include <RenderBase/ProxyHandle.h>

#include <Core/Common/HString.h>
#include <Core/Math/Vector.h>

#include <future>
#include <vector>

namespace hyp::ed
{
    // Document/GPU_Pick_System_Design.md 记录的自定义 render tag——只在 Editor 初始化时注册
    // 一次(见 Editor::Editor()), Engine 侧(RenderTagRegistry/RenderBackend)完全不知道它存在。
    // 跟 hyp::BuiltinRenderTags 同一个写法, 定义放在 PickPass.cpp。
    struct EditorRenderTags
    {
        static const HString Pick;
    };

    // SceneViewport::requestPick() 在逻辑线程按视锥筛出来的每个候选 submesh——pickId 由逻辑线程
    // 分配(见设计文档第 2 节), 不是这里现算的
    struct PickCandidate
    {
        // 两个句柄只有一个有效: 网格候选用 proxyHandle, 四边形候选用 quadProxyHandle
        MeshRenderProxyHandle proxyHandle;
        QuadRenderProxyHandle quadProxyHandle;
        // MeshRenderProxy::getDrawItemsLOD0()(四边形是 getDrawItems())的下标, 对应一个 submesh
        uint32_t subIndex = 0;
        uint32_t pickId = 0;
    };

    // 点击容差半径(像素)——UE 的 HitProxy 拾取也是这个思路: 线/细小物体在屏幕上覆盖的像素
    // 太少, 精确点到那一个像素很难, 所以不是只认点击的那一个像素, 而是认点击点周围这个半径内
    // 离点击点最近的一个命中。同一个值用在两处: SceneViewport::requestPick() 拿它构造一个
    // "点击视锥"缩小 CPU 侧候选收集范围(候选只需要覆盖这个小窗口, 不需要覆盖整个相机视锥),
    // PickPass 拿它决定回读多大一块区域再从中选最近命中(见 PickPass.cpp)。
    static constexpr int32_t kPickToleranceRadiusPixels = 4;

    class PickPass
    {
    public:
        // 按需加入这一帧的主 RenderGraph(没有待处理请求时根本不会被调用), 和 candidates 里每个
        // proxyHandle 关联的 MeshRenderProxy 必须已经注册进 scene——resolve 不到的候选直接跳过,
        // 不当错误处理(可能是请求发起和这一帧执行之间 Component 被销毁)。
        // clickPixelPos 是这个 SceneViewport 内部坐标系下的像素位置——ID 目标本身还是整个
        // viewport 大小(换取实现简单, 见 .cpp 开头的注释), 但回读阶段只读 clickPixelPos 周围
        // kPickToleranceRadiusPixels 半径内的一小块, 取里面离 clickPixelPos 最近的非 0 命中。
        static void addToRenderGraph(
            rg::RenderGraph& renderGraph,
            RendererContext& rendererContext,
            const SceneProxy& scene,
            const SceneView& sceneView,
            const CameraProxy& camera,
            std::vector<PickCandidate> candidates,
            int2 clickPixelPos,
            // shared_ptr 包一层: DeferredTaskQueue::Task 是 std::function<void()>, 要求目标
            // 可拷贝构造, std::promise 本身是 move-only 的, 跟 SkyLightProxy::CaptureRequest::
            // promise 是同一个理由
            std::shared_ptr<std::promise<uint32_t>> promise
        );
    };
}
