#pragma once
#include "EditorCamera.h"
#include "CameraController.h"

#include <HypImGui/ImGuiTextureHandle.h>

#include <Core/Common/Guid.h>

#include <imgui.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace hyp
{
    class Scene;
    class Component;
}

namespace hyp::ed
{
    // requestPick() 的结果——component 为空表示没点中任何东西, 或者对应对象在 GPU 回读结果
    // 回来之前已经被销毁, 两种情况都不当错误处理。见 Document/GPU_Pick_System_Design.md 第 5 节。
    struct PickResult
    {
        Component* component = nullptr;
        // MeshRenderer::getDrawItemsLOD0() 的下标, 只有 component 非空时才有意义
        uint32_t subIndex = 0;
    };
    // 一个视口 = 一个 EditorCamera + 一个 ICameraController + 一张私有的 RenderTarget +
    // 桥接到 ImGui::Image() 的纹理句柄。SceneView 窗口按需持有多个 SceneViewport 实现
    // 分屏, 而不是把 EditorWindow 本身拆成多个实例。
    class SceneViewport
    {
    public:
        // renderTargetName 在渲染图里要求全局唯一(它就是 RenderTarget 的名字, 也是
        // view.getFinalRTName() 用来在 blackboard 里查找导入纹理的 key)
        explicit SceneViewport(HString renderTargetName);

        void setController(std::unique_ptr<ICameraController> controller) noexcept
        {
            mController = std::move(controller);
        }

        // 拿到 PickResult 之后要做什么(比如更新 EditorSelection)完全由调用方决定——这个
        // viewport 自己不知道"选中"是什么意思, 只负责"点了一下, 这是点中的东西"这一件事,
        // 见 Document/GPU_Pick_System_Design.md 关于分层的讨论。请求发起到 callback 真正被调用
        // 之间隔着好几帧的 GPU 回读延迟, 不是同步返回。
        void setPickCallback(std::function<void(PickResult)> callback) noexcept
        {
            mPickCallback = std::move(callback);
        }

        // scene 为 nullptr 表示还没有可用场景(比如还没打开工程), 此时视口只清屏, 不渲染任何内容。
        // availSize 是这个视口在 ImGui 里可用的像素尺寸(不含 padding), 由 SceneView 布局决定——
        // 每次都先 resize() 到这个尺寸, 再决定要不要 attach 到新的 scene, 保证 EditorCamera 第一次
        // 创建 CameraProxy 时用的就是真实尺寸, 不会出现"先以占位的 1x1 建一次、同一帧内马上又
        // 改绑成真实尺寸"这种两步撞车——第一帧真实尺寸能提前拿到的话就没有必要制造这个中间状态。
        void draw(Scene* scene, ImVec2 availSize, float deltaTime);

        // 当前挂着的场景被销毁之前必须先调用, 否则相机的 CameraProxy 会比它所在的 SceneProxy 活得更久
        void detachCamera();

        EditorCamera& getCamera() noexcept
        {
            return mCamera;
        }

    private:
        void resize(uint32_t width, uint32_t height);

        ViewportInput gatherInput(bool hovered) const;

        // Play 时把场景里"画到默认输出"的 Camera(getRenderTarget() == nullptr)临时改绑到
        // 这个视口自己的 RenderTarget 上, 让 gameplay 相机的画面能显示在编辑器视口里; 其余时候
        // (Stop/Pause)把它们停在 0 尺寸, 让位给 EditorCamera。只碰 CameraProxy, 不改
        // Camera 组件自己的反射字段。返回值: 场景里是否至少有一个这样的相机(proxy 已就绪)——
        // Play 时如果压根没有, EditorCamera 不该让位, 不然视口会直接黑屏。
        bool updateDefaultCameras(Scene* scene, bool playing);

        // localPixelPos 是这个 viewport 内部坐标系下的像素位置(左上角为原点)。按当前相机的
        // 视锥在 mAttachedScene 里筛一遍 MeshRenderer, 每个可见的 submesh 分配一个这次请求专属
        // 的 pickId, 交给渲染线程画 PickPass; mPickCallback 在 GPU 回读结果 resolve 完之后才被
        // 调用, 见 SceneViewport.cpp 开头的详细说明。
        void requestPick(int2 localPixelPos);

    private:
        EditorCamera mCamera;
        std::unique_ptr<ICameraController> mController;

        TRef<RenderTarget> mRenderTarget;
        ImGuiTextureHandle mTextureHandle;
        // mExtent: 当前请求的可见尺寸。mCapacityExtent: mRenderTarget 实际分配的纹理尺寸,
        // 恒 >= mExtent——只在 mExtent 超出当前容量时才重建 mRenderTarget(见 resize()
        // 的增长策略), 拖拽窗口途中的尺寸微调不再反复重建 GPU 资源, 只更新
        // EditorCamera::setViewportExtent() 和 ImGui::Image() 的 UV 子矩形。
        uint2 mExtent = uint2(1, 1);
        uint2 mCapacityExtent = uint2(1, 1);
        static constexpr float kCapacityGrowthFactor = 1.5f;
        Scene* mAttachedScene = nullptr;

        // resize 时不在原地销毁重建旧 RenderTarget 的 TextureProxy, 而是整个换一个新
        // RenderTarget 对象, 把旧对象连同一个倒计时丢进这里, 延后几帧再真正释放引用——
        // 见 resize() 里的详细解释: ImGui 用来展示这张纹理的 VkDescriptorSet 摘除本身需要
        // 比 TextureProxy 常规的 kMaxFramesInFlight 更长的安全余量(逻辑线程可能领先渲染线程
        // 好几帧, 用旧 id 又画出好几份还没被消费的 ImDrawData), 旧 RenderTarget 的底层
        // TextureView 必须至少活得和这个 VkDescriptorSet 一样久, 不然会变成销毁一张还被
        // 某个 VkDescriptorSet/命令缓冲区引用着的纹理。
        std::vector<std::pair<TRef<RenderTarget>, int>> mRetiringRenderTargets;
        // 旧 RenderTarget 的 TextureView 释放路径自带一份 kMaxFramesInFlight 延迟,与 ImGui
        // 描述符移除延迟(kMaxFramesInFlight + kMaxQueuedFrames)里的 kMaxFramesInFlight 抵消,
        // 真正需要额外覆盖的只有逻辑线程领先渲染线程的帧数。必须严格大于该值,否则 view 与
        // 描述符会落在同一延迟 tick、销毁顺序不再由帧号保证。
        static constexpr int kRetireFrameCount = kMaxQueuedFrames + 1;

        std::function<void(PickResult)> mPickCallback;
        // 左键从 hover 状态按下时为 true, 松开时清掉——不能靠"按下//松开两次 ImGui::GetMousePos()
        // 绝对坐标"判断有没有拖拽, 见 draw() 里的详细注释(FlyCameraController 按下就会
        // SetCursorCaptured() 切进 GLFW 的 disabled 光标模式)。
        bool mPickButtonCaptured = false;
        bool mGameMouseCaptured = false;
        // 按下瞬间的绝对坐标(那一帧光标还没被 controller 切进 disabled 模式, 真实有效),
        // 松开时没被判定为拖拽就拿它当点击坐标, 不用松开瞬间的坐标。
        ImVec2 mPickMouseDownPos = ImVec2(0.0f, 0.0f);
        // 按住期间每帧 mouseDelta 绝对值的累计——disabled 光标模式下唯一还可靠的位移信号,
        // 超过这个阈值就判定为拖拽(相机看向操作), 不触发 pick
        float mPickDragAccum = 0.0f;
        static constexpr float kPickDragThresholdPixels = 4.0f;
    };
}
