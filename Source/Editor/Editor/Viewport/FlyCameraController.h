#pragma once
#include "CameraController.h"

namespace hyp::ed
{
    // 常见编辑器飞行相机: 按住左键或右键才会捕获光标(Window::captureCursor(), 隐藏 OS 光标
    // 并解除屏幕边界限制, 看方向时不会因为鼠标撞到屏幕边缘就转不动了)、开始响应鼠标看方向和
    // WASD/QE(上/下)移动; 松开后释放光标(Window::releaseCursor(), 光标回到按下时的位置),
    // 视角停在当前朝向, 移动也随之停止。
    class FlyCameraController : public ICameraController
    {
    public:
        void tick(EditorCamera& camera, const ViewportInput& input, float deltaTime) override;

        void setMoveSpeed(float speed) noexcept
        {
            mMoveSpeed = speed;
        }

        float getMoveSpeed() const noexcept
        {
            return mMoveSpeed;
        }

        void setMouseSensitivity(float sensitivity) noexcept
        {
            mMouseSensitivity = sensitivity;
        }

    private:
        bool mInitialized = false;
        bool mWasLookActive = false;
        bool mLookButtonCaptured[2] = { false, false };
        // 上一帧是不是刚从非看方向状态切换成看方向状态; tick() 用它跳过"上一帧刚激活"
        // 之后紧接着这一帧的 delta, 见 tick() 里的用法注释。
        bool mJustActivatedLastFrame = false;
        float mYaw = 0.0f;
        float mPitch = 0.0f;
        float mMoveSpeed = 10.0f;
        float mMouseSensitivity = 0.15f;
    };
}
