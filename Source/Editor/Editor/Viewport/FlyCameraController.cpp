#include "FlyCameraController.h"
#include "EditorCamera.h"

#include <Core/Common/LogSystem.h>

#include <Engine/Context.h>
#include <Engine/Input/InputSystem.h>

#include <HypImGui/ImGuiDragCapture.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace hyp::ed
{
    namespace
    {
        constexpr int kMouseButtonLeft = 0;
        constexpr int kMouseButtonRight = 1;
        constexpr float3 kWorldUp(0.0f, 0.0f, 1.0f);

        // 对应 UE FEditorViewportClient::OnChangeCameraSpeed() 的调速策略: 按住鼠标飞行时
        // 滚一格滚轮把当前速度乘以 1.1(上滚)/0.9(下滚), 是乘法不是加法——数值越大, 同样滚
        // 一格增减的绝对量也越大, 任何速度区间调整起来手感一致。上下限对应 UE
        // FEditorViewportCameraSpeedSettings 的 AbsoluteMinSpeed/AbsoluteMaxSpeed 默认值,
        // 本质上是防止用滚轮把速度滚到 0 或溢出的安全钳位, 不是真的期望用到这两个极值。
        constexpr float kSpeedScrollUpFactor = 1.1f;
        constexpr float kSpeedScrollDownFactor = 0.9f;
        constexpr float kMinMoveSpeed = 0.00001f;
        constexpr float kMaxMoveSpeed = 10000.0f;
    }

    void FlyCameraController::tick(EditorCamera& camera, const ViewportInput& input, float deltaTime)
    {
        // 只有从视口内部开始的按键才捕获; 从标题栏/Dock 标签开始的拖拽即使后来经过
        // 视口也不会中途激活相机。捕获后允许光标移出视口, 直到对应按键释放。
        if (input.hovered)
        {
            if (input.mousePressed[kMouseButtonLeft])
                mLookButtonCaptured[kMouseButtonLeft] = true;
            if (input.mousePressed[kMouseButtonRight])
                mLookButtonCaptured[kMouseButtonRight] = true;
        }
        if (!input.mouseDown[kMouseButtonLeft])
            mLookButtonCaptured[kMouseButtonLeft] = false;
        if (!input.mouseDown[kMouseButtonRight])
            mLookButtonCaptured[kMouseButtonRight] = false;

        bool lookActive =
            mLookButtonCaptured[kMouseButtonLeft] ||
            mLookButtonCaptured[kMouseButtonRight];

        // 光标捕获(GLFW_CURSOR_DISABLED: 隐藏 OS 光标、解除屏幕边界限制, 只报相对位移)跟
        // lookActive 严格同步——按住左键或右键立刻捕获, 松开立刻释放(光标回到按下时的位置,
        // GLFW 在 Disabled<->Normal 之间切换的既有行为)。
        // 同时顺带关掉 ImGuiConfigFlags_NoMouse: 捕获期间光标是不受屏幕边界限制的虚拟坐标,
        // 会飘到其它窗口的控件上方触发 hover 高亮, 见 SetCursorCaptured() 的注释。
        //
        // 只在 lookActive 真正变化的这一帧调用 SetCursorCaptured(), 不是每帧无条件调用——
        // 无条件调用会跟其它同样在用 SetCursorCaptured()/CaptureCursorDuringDrag() 的调用方
        // (比如 Inspector 里的 DragXXX 控件)互相抢状态: 那些控件释放时光标本该保持捕获,
        // 却会被这里每帧一次的调用连带覆盖掉。
        Window& window = Context::get().getInputSystem().getWindow();
        if (lookActive != mWasLookActive)
            SetCursorCaptured(window, lookActive);

        // 只有刚开始拖拽的这一帧(以及 controller 从没同步过时)才从相机当前朝向重新推出
        // yaw/pitch——相机可能在没有拖拽的这段时间里被外部代码(比如场景切换、脚本、编辑器
        // 的测试工具)直接改过朝向, 不重新同步的话这里会拿着上一次拖拽结束时的旧 yaw/pitch,
        // 一开始拖拽就把相机瞬间掰回旧朝向。
        bool justActivated = lookActive && !mWasLookActive;
        if (!mInitialized || justActivated)
        {
            float3 forward = camera.getForward();
            mPitch = glm::degrees(std::asin(std::clamp(forward.z, -1.0f, 1.0f)));
            mYaw = glm::degrees(std::atan2(-forward.x, forward.y));
            mInitialized = true;
        }
        mWasLookActive = lookActive;

        // 没有拖拽时不再往下写相机——只有真正处于飞行状态才需要这个 controller 决定相机
        // 位姿, 空闲时把相机完全让给外部改动(否则任何没有拖拽的这一帧都会用上面刚同步出的
        // yaw/pitch 把相机原样摊回去, 看起来就是"外部设置不起作用")。
        if (!lookActive)
        {
            mJustActivatedLastFrame = false;
            return;
        }

        // 刚激活的这一帧本身的 io.MouseDelta 是干净的; 真正需要跳过的是它的下一帧——
        // ImGui 的输入事件重放规则(UpdateInputEvents() 里"这一帧已经处理过一次按键变化就
        // break")会把按下瞬间还没来得及消费的鼠标移动事件推迟到下一帧才一次性回补进
        // io.MouseDelta, 直接套用会表现成一次跳变。
        if (!mJustActivatedLastFrame)
        {
            mYaw -= input.mouseDeltaX * mMouseSensitivity;
            mPitch -= input.mouseDeltaY * mMouseSensitivity;
            mPitch = std::clamp(mPitch, -89.0f, 89.0f);
        }
        mJustActivatedLastFrame = justActivated;

        // UE 用 pow() 泛化到分数级滚动量: 高精度滚轮/触控板单帧的 scrollY 不一定是
        // 整数 1.0, 按比例套用同一个每格系数, 效果上跟连续滚动整数格等价。
        if (input.scrollY != 0.0f)
        {
            float factor = std::pow(
                input.scrollY > 0.0f ? kSpeedScrollUpFactor : kSpeedScrollDownFactor,
                std::abs(input.scrollY)
            );
            mMoveSpeed = std::clamp(mMoveSpeed * factor, kMinMoveSpeed, kMaxMoveSpeed);
            LOG_INFO("FlyCameraController: move speed = {:.3f}", mMoveSpeed);
        }

        float yawRad = glm::radians(mYaw);
        float pitchRad = glm::radians(mPitch);
        float cosPitch = std::cos(pitchRad);
        float3 forward = glm::normalize(float3(
            -std::sin(yawRad) * cosPitch,
            std::cos(yawRad) * cosPitch,
            std::sin(pitchRad)
        ));
        float3 right = glm::normalize(glm::cross(forward, kWorldUp));

        double3 position = camera.getPosition();
        float speed = mMoveSpeed * deltaTime;
        if (input.moveForward) position += double3(forward) * double(speed);
        if (input.moveBackward) position -= double3(forward) * double(speed);
        if (input.moveRight) position += double3(right) * double(speed);
        if (input.moveLeft) position -= double3(right) * double(speed);
        if (input.moveUp) position += double3(kWorldUp) * double(speed);
        if (input.moveDown) position -= double3(kWorldUp) * double(speed);

        camera.setOrientation(forward, kWorldUp);
        camera.setPosition(position);
    }
}
