#include "SceneViewport.h"
#include "FlyCameraController.h"
#include "../RenderPass/PickPass.h"

#include <Engine/Context.h>
#include <Engine/Framework/Scene.h>
#include <Engine/Framework/Entity.h>
#include <Engine/Component/Camera/Camera.h>
#include <Engine/Component/Renderer/MeshRenderer.h>
#include <Engine/Component/Renderer/QuadRenderer.h>

#include <Renderer/CameraProxy.h>
#include <Renderer/SceneProxy.h>
#include <RenderBase/RenderFrontend.h>
#include <RenderBase/TextureProxy.h>

#include <Core/Math/Frustum.h>

#include <HypImGui/ImGuiFrontend.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace hyp::ed
{
    namespace
    {
        // ==== TEST WINDOW BEGIN: 精确设置 EditorCamera 位置/旋转/FOV、强制视口像素尺寸的调试
        // 小窗口, 整块删掉(这个 namespace 块 + draw() 里 applyForcedViewportSize() 那一行 +
        // 末尾的 drawEditorTestWindow() 调用)即可移除, 不影响其他代码。
        // FlyCameraController 只在实际拖拽时才写相机(见该文件), 空闲时不会覆盖这里设的值,
        // 所以不需要额外开关去互斥两者。

        // (0,0) 表示不强制, 跟随窗口实际可用尺寸——由测试窗口的按钮设置, 下一帧的
        // applyForcedViewportSize() 才会用上, 慢一帧对测试工具无所谓。
        ImVec2 gForcedViewportSize(0.0f, 0.0f);

        // 直接把渲染分辨率钉死成设定值, 不是去改 SceneView 这个 ImGui 窗口本身的大小——
        // 后者要考虑标题栏/Dock 边框的额外像素, 换算麻烦还不一定准。这样渲染出来的图永远是
        // 精确的目标像素数, 方便跟 UE 逐像素对比; 如果 SceneView 面板本身没那么大, ImGui 会
        // 自动出滚动条, 把面板拖大/浮动出来就能看到完整画面。
        ImVec2 applyForcedViewportSize(ImVec2 availSize)
        {
            if (gForcedViewportSize.x > 0.0f && gForcedViewportSize.y > 0.0f)
                return gForcedViewportSize;
            return availSize;
        }

        // 构造一份"点击视锥"专用的偏心投影矩阵——只把点击点周围一个小窗口(而不是整个 viewport)
        // 映射到完整的 clip space, 喂给现成的 Frustumd 就能把候选收集范围从整个相机视锥缩小到
        // 这个小窗口, 不用改 Frustum.h 本身(Gribb-Hartmann 平面提取公式对偏心/带斜切投影矩阵
        // 一样成立, 这个引擎自己的 TAA jitter 已经在往同一组矩阵分量——GLM 的
        // result[2][0]/result[2][1]——里塞非零值, 且喂给 Frustumd 用了很久没出过剔除问题,
        // 见跟用户的讨论)。
        //
        // 公式推导(标准数学记号 M[row][col], 下面用 GLM 的 result[col][row] 存取):
        // 目标: clip_new.xy / w == zoom * (clip_old.xy / w - centerNdc), 且 w_new = w_old = -z
        // (跟原矩阵一致, 不改近远面处理)。展开:
        //   clip_new.x = zoom.x * clip_old.x - zoom.x * centerNdc.x * w
        //              = zoom.x * clip_old.x + zoom.x * centerNdc.x * z   (w = -z)
        // clip_old.x = M[0][0]*x(其余项原本是 0), 所以:
        //   新 M[0][0] = zoom.x * 原 M[0][0]        → result[0][0] = zoomX * base[0][0]
        //   新 M[0][2] = zoom.x * centerNdc.x       → result[2][0] = zoomX * centerNdc.x
        // y 分量同理。这组符号已经用现有的 TAA jitter 代码(CameraProxy.cpp 里
        // "jitteredProjectionMatrix[2][0] -= jitterOffsetNDC.x" 那几行, 对应
        // "新 M[0][2] = -jitterOffsetNDC.x") 交叉验证过, 不是凭空假设。
        //
        // 像素→NDC 换算跟这两行反过来: 屏幕像素(左上角原点, Y 向下)要经过 Vulkan 后端那个
        // "viewport.height = -height" 的翻转(Vulkan::CommandList::setViewportBackend())才变成
        // 这个引擎 ViewData/Frustum 全程假设的、跟 glm 一致的 Y-up NDC——所以 Y 这里要翻一次号,
        // X 不用。
        float4x4 makePickProjection(const float4x4& baseProjection, uint2 viewportExtent, int2 clickPixelPos, int32_t windowRadiusPixels)
        {
            float ndcX = (float(clickPixelPos.x) / float(viewportExtent.x)) * 2.0f - 1.0f;
            float ndcY = 1.0f - (float(clickPixelPos.y) / float(viewportExtent.y)) * 2.0f;

            float zoomX = float(viewportExtent.x) / (float(windowRadiusPixels) * 2.0f);
            float zoomY = float(viewportExtent.y) / (float(windowRadiusPixels) * 2.0f);

            float4x4 result = baseProjection;
            result[0][0] = baseProjection[0][0] * zoomX;
            result[1][1] = baseProjection[1][1] * zoomY;
            result[2][0] = ndcX * zoomX;
            result[2][1] = ndcY * zoomY;
            return result;
        }

        void drawEditorTestWindow(EditorCamera& camera, Scene* scene)
        {
            // 和 position 一样每帧都从相机当前状态反算, 不缓存——飞行相机转动视角时这里
            // 也要跟着刷新, 不然只有 position 实时、yaw/pitch/FOV 卡在上次窗口自己改过的值。
            float3 forward = camera.getForward();
            float pitch = glm::degrees(std::asin(std::clamp(forward.z, -1.0f, 1.0f)));
            float yaw = glm::degrees(std::atan2(-forward.x, forward.y));
            float fov = camera.getFov();

            if (ImGui::Begin("[Test] Camera & Viewport"))
            {
                double3 position = camera.getPosition();
                bool changed = false;
                changed |= ImGui::InputDouble("Position X", &position.x);
                changed |= ImGui::InputDouble("Position Y", &position.y);
                changed |= ImGui::InputDouble("Position Z", &position.z);
                changed |= ImGui::DragFloat("Yaw", &yaw, 0.1f);
                changed |= ImGui::DragFloat("Pitch", &pitch, 0.1f, -89.0f, 89.0f);
                changed |= ImGui::DragFloat("FOV", &fov, 0.1f, 1.0f, 179.0f);

                if (changed)
                {
                    float yawRad = glm::radians(yaw);
                    float pitchRad = glm::radians(pitch);
                    float cosPitch = std::cos(pitchRad);
                    float3 forward = glm::normalize(float3(
                        -std::sin(yawRad) * cosPitch,
                        std::cos(yawRad) * cosPitch,
                        std::sin(pitchRad)));
                    camera.setOrientation(forward, float3(0.0f, 0.0f, 1.0f));
                    camera.setPosition(position);
                    camera.setPerspective(fov, camera.getNearPlane(), camera.getFarPlane());
                }

                ImGui::Separator();
                static int forcedWidth = 1920;
                static int forcedHeight = 1080;
                ImGui::InputInt("Width", &forcedWidth);
                ImGui::InputInt("Height", &forcedHeight);
                if (ImGui::Button("Apply"))
                    gForcedViewportSize = ImVec2(
                        static_cast<float>(std::max(forcedWidth, 1)),
                        static_cast<float>(std::max(forcedHeight, 1)));
                ImGui::SameLine();
                if (ImGui::Button("Clear"))
                    gForcedViewportSize = ImVec2(0.0f, 0.0f);

                if (scene)
                {
                    ImGui::Separator();
                    bool lumenEnabled = scene->isLumenEnabled();
                    if (ImGui::Checkbox("Lumen Enabled", &lumenEnabled))
                        scene->setLumenEnabled(lumenEnabled);

                    // 顺序跟 hyp::SceneVisualizeMode 的枚举值一一对应。中间 14 项只在 Lumen
                    // Enabled 时才生效(见 RenderPipeline.cpp 的分派), 后五项(屏幕空间追踪/
                    // 屏幕探针调试)跟这个开关完全无关, 一直生效
                    static const char* kSceneVisualizeModeNames[] = {
                        "None", "Geometry Normal", "Albedo", "Normal", "Emissive", "Card Weights", "Direct Lighting", "Indirect Lighting", "Final Lighting",
                        "Atlas Albedo", "Atlas Normal", "Atlas Depth", "Atlas Direct Lighting", "Atlas Indirect Lighting", "Atlas Final Lighting",
                        "Screen Trace Hit State", "Screen Trace Radiance", "Screen Probe Placement", "Screen Probe Radiance", "Screen Probe Irradiance",
                        "Screen Probe BRDF PDF"
                    };
                    int sceneVisualizeMode = static_cast<int>(scene->getSceneVisualizeMode());
                    if (ImGui::Combo("Visualize", &sceneVisualizeMode, kSceneVisualizeModeNames, IM_ARRAYSIZE(kSceneVisualizeModeNames)))
                        scene->setSceneVisualizeMode(static_cast<SceneVisualizeMode>(sceneVisualizeMode));

                    // 跟上面的 Combo 正交: 除了 Atlas 系列(不追踪, 这个开关对它们没有效果),
                    // 其余每一项命中着色都能在这两种追踪方式之间切换
                    bool useMeshDistanceField = scene->getUseMeshDistanceField();
                    if (ImGui::Checkbox("Use Mesh Distance Field (MDF)", &useMeshDistanceField))
                        scene->setUseMeshDistanceField(useMeshDistanceField);

                    bool disableScreenTrace = scene->getDisableScreenTrace();
                    if (ImGui::Checkbox("Disable Screen Trace", &disableScreenTrace))
                        scene->setDisableScreenTrace(disableScreenTrace);
                }
            }
            ImGui::End();
        }
        // ==== TEST WINDOW END ====
    }

    SceneViewport::SceneViewport(HString renderTargetName) :
        mController(std::make_unique<FlyCameraController>())
    {
        mRenderTarget = createObject<RenderTarget>(nullptr);
        mRenderTarget->setName(renderTargetName);
        mRenderTarget->setFormat(PixelFormat::RGBA8_UNORM);
        mRenderTarget->setSize(mExtent.x, mExtent.y);

        mCamera.setRenderTarget(mRenderTarget.get(), mCapacityExtent);
        mCamera.setPosition(double3(0.0, -4.0, 1.5));
        mCamera.setOrientation(float3(0.0f, 1.0f, 0.0f), float3(0.0f, 0.0f, 1.0f));
        mCamera.setPerspective(90.0f, 0.01f, 0.0f);
    }

    void SceneViewport::resize(uint32_t width, uint32_t height)
    {
        width = std::max(width, 1u);
        height = std::max(height, 1u);
        if (mExtent.x == width && mExtent.y == height)
            return;

        mExtent = uint2(width, height);

        if (mExtent.x <= mCapacityExtent.x && mExtent.y <= mCapacityExtent.y)
        {
            // 请求尺寸没有超出当前已分配的容量纹理——只挪动相机的可见视口矩形
            // (EditorCamera::setViewportExtent() 更新 outputRect), 不碰 mRenderTarget/
            // TextureProxy, 避免拖拽窗口时每帧都创建/销毁一次离屏纹理。
            mCamera.setViewportExtent(mExtent);
            return;
        }

        // 容量不够, 需要重新分配一张更大的纹理。按 kCapacityGrowthFactor 多留一些余量,
        // 而不是精确按这次请求的尺寸分配, 减少继续拖大过程中再次触发重建的次数。
        mCapacityExtent = uint2(
            std::max(mExtent.x, static_cast<uint32_t>(mCapacityExtent.x * kCapacityGrowthFactor)),
            std::max(mExtent.y, static_cast<uint32_t>(mCapacityExtent.y * kCapacityGrowthFactor))
        );

        // 摘除旧 ImGui 描述符要先入队, 再丢弃旧 RenderTarget——具体原因见 reset() 和
        // mRetiringRenderTargets 的注释。
        Context::get().getRenderFrontend().enqueueLambda(
            [textureHandle = &mTextureHandle](FrameContext&)
            {
                textureHandle->reset();
            }
        );

        // 不在原地 resize 旧 RenderTarget(销毁重建同一个 TextureProxy handle), 而是整个
        // 换一张新的——旧对象连同一个倒计时放进 mRetiringRenderTargets, 按自己的节奏
        // 再活几帧才真正释放引用, 见该成员的类声明注释。
        HString renderTargetName = mRenderTarget->getName();
        mRetiringRenderTargets.emplace_back(std::move(mRenderTarget), kRetireFrameCount);

        mRenderTarget = createObject<RenderTarget>(nullptr);
        mRenderTarget->setName(renderTargetName);
        mRenderTarget->setFormat(PixelFormat::RGBA8_UNORM);
        mRenderTarget->setSize(mCapacityExtent.x, mCapacityExtent.y);

        TextureProxyHandle proxyHandle = mRenderTarget->getOrCreateTextureProxy();
        Context::get().getRenderFrontend().enqueueLambda(
            [proxyHandle, textureHandle = &mTextureHandle](FrameContext& context)
            {
                TextureProxy* proxy = context.getProxyManager().getProxy(proxyHandle);
                textureHandle->update(proxy->getTextureView());
            }
        );

        mCamera.setRenderTarget(mRenderTarget.get(), mCapacityExtent);
        mCamera.setViewportExtent(mExtent);
    }

    void SceneViewport::detachCamera()
    {
        mCamera.attachToScene(nullptr);
        mAttachedScene = nullptr;
    }

    void SceneViewport::draw(Scene* scene, ImVec2 availSize, float deltaTime)
    {
        for (auto& entry : mRetiringRenderTargets)
            --entry.second;
        std::erase_if(mRetiringRenderTargets, [](const auto& entry) { return entry.second <= 0; });

        availSize = applyForcedViewportSize(availSize); // TEST WINDOW: 见文件顶部, 删除时去掉这一行
        resize(static_cast<uint32_t>(availSize.x), static_cast<uint32_t>(availSize.y));

        bool playing = scene && scene->isPlaying();

        // 场景里得真有相机接过去画, EditorCamera 才需要让位——没有默认输出相机(比如
        // 场景压根没放相机)的话继续用 EditorCamera 兜底, 不然 Play 时视口直接黑屏,
        // 看起来像"动画没在播", 其实只是没人画
        bool hasDefaultCamera = scene && updateDefaultCameras(scene, playing);

        // Play 期间且确实有相机接管时才不再把 EditorCamera 挂到场景上——它和被
        // updateDefaultCameras() 改绑过来的 gameplay 相机会抢着往同一张 mRenderTarget 上画,
        // 见该函数注释。Pause 时重新挂回去(对应挂的是 Play 出来的那份场景, 不是编辑场景),
        // 场景冻结但相机能自由观察。
        Scene* desiredAttachedScene = (playing && hasDefaultCamera) ? nullptr : scene;
        if (desiredAttachedScene != mAttachedScene)
        {
            mCamera.attachToScene(desiredAttachedScene);
            mAttachedScene = desiredAttachedScene;
        }

        ImTextureID textureId = mTextureHandle.get();
        if (textureId)
        {
            // 容量纹理可能比这次实际请求的可见尺寸大——只显示左上角 mExtent 对应的那一块,
            // 和 EditorCamera::computeOutputRect() 渲染进去的子矩形保持一致。
            ImVec2 uv1(
                static_cast<float>(mExtent.x) / static_cast<float>(mCapacityExtent.x),
                static_cast<float>(mExtent.y) / static_cast<float>(mCapacityExtent.y)
            );
            ImGui::Image(textureId, availSize, ImVec2(0.0f, 0.0f), uv1);
        }
        else
        {
            // 离屏纹理还没被渲染线程建出来(刚打开窗口的第一帧), 先画个占位方块, 避免
            // 布局跳动
            ImGui::Dummy(availSize);
        }

        bool hovered = ImGui::IsItemHovered();
        // 这一帧的 item rect 只在这里(以及下面读它的这一行)有效——接下来的
        // drawEditorTestWindow() 等调用会改变"上一个 item"是谁。
        ImVec2 itemMin = ImGui::GetItemRectMin();

        ViewportInput input = gatherInput(hovered);

        // 右键/中键按下不会让 ImGui 切换焦点窗口, 不主动聚焦的话别的窗口里的文本框会继续接收按键
        if (hovered && (input.mousePressed[1] || input.mousePressed[2]))
            ImGui::SetWindowFocus();

        const bool gameOwnsViewport = playing && hasDefaultCamera;
        const bool anyMouseDown = input.mouseDown[0] || input.mouseDown[1] || input.mouseDown[2];
        if (gameOwnsViewport && hovered && (input.mousePressed[0] || input.mousePressed[1] || input.mousePressed[2]))
            mGameMouseCaptured = true;
        if (!gameOwnsViewport || !anyMouseDown)
            mGameMouseCaptured = false;
        ImGuiFrontend::setGameInputActive(
            gameOwnsViewport && (hovered || mGameMouseCaptured),
            gameOwnsViewport && ImGui::IsWindowFocused()
        );

        // 点击拾取——不能用"按下/松开两次 ImGui::GetMousePos() 的绝对坐标差"判断有没有拖拽:
        // FlyCameraController 只要按下左键就会立刻 SetCursorCaptured() 切进 GLFW 的
        // GLFW_CURSOR_DISABLED 模式(隐藏 OS 光标、解除屏幕边界限制、只报相对位移), 松开时又会
        // 把光标直接挪回按下时的位置——按住期间读到的绝对坐标是不受约束的合成值, 没有意义。
        // 真正可靠的信号是 mouseDelta 的累计量(disabled 模式下这是唯一还在正常工作的东西,
        // FlyCameraController 自己判断有没有在转向用的也是这个)。点击坐标同理改用按下瞬间的
        // 绝对坐标(这一帧 SetCursorCaptured() 还没被 controller 调用过, 真实有效), 不用松开
        // 瞬间的坐标。
        if (hovered && input.mousePressed[0])
        {
            mPickButtonCaptured = true;
            mPickMouseDownPos = ImGui::GetMousePos();
            mPickDragAccum = 0.0f;
        }
        if (mPickButtonCaptured)
        {
            mPickDragAccum += std::abs(input.mouseDeltaX) + std::abs(input.mouseDeltaY);
            if (!input.mouseDown[0])
            {
                mPickButtonCaptured = false;
                if (mPickCallback && mPickDragAccum <= kPickDragThresholdPixels)
                {
                    int2 localPixelPos(
                        int32_t(mPickMouseDownPos.x - itemMin.x),
                        int32_t(mPickMouseDownPos.y - itemMin.y)
                    );
                    requestPick(localPixelPos);
                }
            }
        }

        // EditorCamera 没有挂在任何场景上(desiredAttachedScene 为 nullptr)时不参与渲染,
        // 飞行控制器没有意义, 也会跟 gameplay 自己的输入处理抢 WASD/鼠标——直接不 tick。
        if (mController && mAttachedScene)
        {
            mController->tick(mCamera, input, deltaTime);
        }

        drawEditorTestWindow(mCamera, scene); // TEST WINDOW: 见文件顶部同名函数, 删除时一并去掉
    }

    bool SceneViewport::updateDefaultCameras(Scene* scene, bool playing)
    {
        bool hasDefaultCamera = false;

        TextureProxyHandle targetProxyHandle;
        HString targetName;
        uint2 targetExtent(0, 0);
        if (playing)
        {
            targetProxyHandle = mRenderTarget->getOrCreateTextureProxy();
            targetName = mRenderTarget->getName();
            // 用 mExtent(可见尺寸)而不是 mCapacityExtent(纹理实际分配尺寸)——gameplay
            // Camera 的 OutputRect 默认是整个 [0,0,1,1], 传 mExtent 让它按跟 EditorCamera
            // 同样的方式只画进容量纹理左上角 mExtent 大小的那块, 和 ImGui::Image() 的 UV
            // 子矩形保持一致。
            targetExtent = mExtent;
        }

        for (const TRef<Entity>& entity : scene->getEntities())
        {
            for (Camera* camera : entity->getComponents<Camera>())
            {
                // getRenderTarget() 非空说明这个相机本来就有明确的渲染目标(比如小地图),
                // 不属于"默认输出"的范畴, 不需要也不应该改它
                if (camera->getRenderTarget())
                    continue;

                CameraProxyHandle proxyHandle = camera->getProxyHandle();
                if (!proxyHandle.isValid())
                    continue; // 这一帧还没注册完, 下一帧再试

                hasDefaultCamera = true;

                Context::get().getRenderFrontend().enqueueLambda(
                    [proxyHandle, playing, targetProxyHandle, targetName, targetExtent](FrameContext& context)
                    {
                        CameraProxy* cameraProxy = context.getProxyManager().getProxy(proxyHandle);
                        if (!cameraProxy)
                            return;

                        if (playing)
                        {
                            TextureProxy* targetProxy = context.getProxyManager().getProxy(targetProxyHandle);
                            cameraProxy->setRenderTarget(targetProxy, targetName, targetExtent, OutputRouting::Direct_Write);
                        }
                        else
                        {
                            cameraProxy->setRenderTargetExtent(uint2(0, 0));
                        }
                    }
                );
            }
        }

        return hasDefaultCamera;
    }

    ViewportInput SceneViewport::gatherInput(bool hovered) const
    {
        ViewportInput input;
        input.hovered = hovered;

        ImGuiIO& io = ImGui::GetIO();
        input.mouseDeltaX = io.MouseDelta.x;
        input.mouseDeltaY = io.MouseDelta.y;
        input.scrollY = io.MouseWheel;
        input.mousePressed[0] = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        input.mousePressed[1] = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        input.mousePressed[2] = ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        input.mouseDown[0] = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        input.mouseDown[1] = ImGui::IsMouseDown(ImGuiMouseButton_Right);
        input.mouseDown[2] = ImGui::IsMouseDown(ImGuiMouseButton_Middle);

        // WASD/QE 原始按键状态无条件采集, 不按 hovered 过滤——跟 mouseDeltaX/scrollY 一样,
        // 交给下游 FlyCameraController::tick() 按 lookActive(是否按住左键/右键、光标是否
        // 已经被 captureCursor() 捕获)决定要不要用。这里不能用 hovered 把关: 一旦真正进入
        // 捕获状态, GLFW 的 GLFW_CURSOR_DISABLED 模式会让光标位置变成无边界的虚拟坐标,
        // ImGui::IsItemHovered() 在飞行过程中几乎立刻就会判定成 false(鼠标一动就可能报出
        // 视口范围外的坐标), 如果这里还按 hovered 过滤, 正在飞行时移动反而会被意外掐断。
        input.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
        input.moveBackward = ImGui::IsKeyDown(ImGuiKey_S);
        input.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
        input.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
        input.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
        input.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);

        return input;
    }

    void SceneViewport::requestPick(int2 localPixelPos)
    {
        if (!mPickCallback || !mAttachedScene)
            return;

        // 候选收集不用整个相机视锥, 用点击点周围一个小窗口对应的"点击视锥"——跟 PickPass 回读
        // 阶段用的是同一个容差半径(kPickToleranceRadiusPixels, 见 PickPass.h), 候选只需要覆盖
        // 这个小窗口, 不需要覆盖整个相机视锥。矩阵合成方式(proj * view)跟 SceneView.cpp:68 的
        // mFrustum 构造一致, 只是 proj 换成了 makePickProjection() 算出来的偏心版本, 不是另起
        // 一套约定。
        float4x4 pickProjection = makePickProjection(mCamera.getProjectionMatrix(), mExtent, localPixelPos, kPickToleranceRadiusPixels);
        Frustumd frustum(double4x4(pickProjection) * mCamera.getViewMatrix());

        std::vector<PickCandidate> candidates;
        // pickId 是这次请求专属的临时数据, 不追求跨帧稳定——resolve 完(或者判定为过期)就整个
        // 丢弃, 见 Document/GPU_Pick_System_Design.md 第 1 节
        std::unordered_map<uint32_t, std::pair<Guid, uint32_t>> resolveTable;
        uint32_t nextPickId = 1; // 0 保留给"没点中任何东西"

        for (const TRef<Entity>& entity : mAttachedScene->getEntities())
        {
            for (QuadRenderer* quadRenderer : entity->getComponents<QuadRenderer>())
            {
                QuadRenderProxyHandle quadProxyHandle = quadRenderer->getProxyHandle();
                if (!quadProxyHandle.isValid() || !frustum.contains(quadRenderer->computeWorldBoundingBox()))
                    continue;

                uint32_t pickId = nextPickId++;
                candidates.push_back(PickCandidate{ MeshRenderProxyHandle(), quadProxyHandle, 0, pickId });
                resolveTable.emplace(pickId, std::make_pair(quadRenderer->getGuid(), 0u));
            }

            for (MeshRenderer* meshRenderer : entity->getComponents<MeshRenderer>())
            {
                MeshRenderProxyHandle proxyHandle = meshRenderer->getProxyHandle();
                if (!proxyHandle.isValid())
                    continue;

                if (!frustum.contains(meshRenderer->computeWorldBoundingBox()))
                    continue;

                uint32_t submeshCount = meshRenderer->getSubmeshCount();
                Guid componentGuid = meshRenderer->getGuid();
                for (uint32_t subIndex = 0; subIndex < submeshCount; ++subIndex)
                {
                    uint32_t pickId = nextPickId++;
                    candidates.push_back(PickCandidate{ proxyHandle, QuadRenderProxyHandle(), subIndex, pickId });
                    resolveTable.emplace(pickId, std::make_pair(componentGuid, subIndex));
                }
            }
        }

        if (candidates.empty())
        {
            mPickCallback(PickResult{});
            return;
        }

        auto promise = std::make_shared<std::promise<uint32_t>>();
        std::future<uint32_t> future = promise->get_future();

        CameraProxyHandle cameraProxyHandle = mCamera.getProxyHandle();
        SceneProxyHandle sceneProxyHandle = mCamera.getAttachedSceneProxyHandle();

        Context::get().getRenderFrontend().enqueueLambda(
            [cameraProxyHandle, sceneProxyHandle, candidates = std::move(candidates), localPixelPos, promise](FrameContext& context) mutable
            {
                auto& proxyManager = context.getProxyManager();
                CameraProxy* cameraProxy = proxyManager.getProxy(cameraProxyHandle);
                SceneProxy* sceneProxy = proxyManager.getProxy(sceneProxyHandle);
                if (cameraProxy && sceneProxy)
                {
                    if (const hyp::SceneView* sceneView = cameraProxy->getLastSceneView())
                    {
                        PickPass::addToRenderGraph(
                            context.getRenderGraph(),
                            Context::get().getRendererContext(),
                            *sceneProxy,
                            *sceneView,
                            *cameraProxy,
                            std::move(candidates),
                            localPixelPos,
                            promise
                        );
                        return;
                    }
                }
                // proxy 还没就绪/这一帧没有有效 SceneView——没有东西可点, 不是错误
                promise->set_value(0);
            }
        );

        // requestScene 在请求发起时就固定下来, 不读 resolve 时的 mAttachedScene——回调触发前
        // 场景可能已经切换, 用切换后的 Scene 去 findComponent() 会拿一份跟当前 resolveTable
        // 毫不相干的查找结果, 必须用请求发起那一刻的 Scene。
        // 捕获 this 是有意的: mPickCallback 由 SceneView 设置, 只要这个 SceneViewport(SceneView
        // 的成员)还活着回调就有意义, 跟这个类其它 enqueueLambda 捕获裸 this/成员指针是同一个
        // 生命周期假设。
        Scene* requestScene = mAttachedScene;
        Context::get().getRenderFrontend().onFutureReady(
            std::move(future),
            [this, requestScene, resolveTable = std::move(resolveTable)](uint32_t rawPickId)
            {
                PickResult result;
                if (rawPickId != 0)
                {
                    auto it = resolveTable.find(rawPickId);
                    if (it != resolveTable.end())
                    {
                        if (Component* component = requestScene->findComponent(it->second.first))
                        {
                            result.component = component;
                            result.subIndex = it->second.second;
                        }
                    }
                }
                mPickCallback(result);
            }
        );
    }
}
