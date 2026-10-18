#pragma once

namespace hyp::ed
{
    class EditorCamera;

    // SceneViewport 每帧从 ImGui::GetIO() 采集的原始输入快照——除 hovered 本身外, 其它字段
    // 都是无条件采集的原始按键/鼠标状态, 要不要用由具体 ICameraController 自己按 hovered/
    // 是否已经按住鼠标键(比如 FlyCameraController::lookActive)去决定, 不在这一层过滤。用
    // ImGui 自己的输入状态而不是引擎 InputSystem 的事件队列/PlayerInput 动作映射——后者是
    // 为游戏输入设计的, 编辑器视口只是"这块 ImGui::Image 被 hover 时读一下鼠标键盘", 没有
    // 必要额外走一条事件队列。
    struct ViewportInput
    {
        bool hovered = false;
        float mouseDeltaX = 0.0f;
        float mouseDeltaY = 0.0f;
        float scrollY = 0.0f;
        bool mousePressed[3] = { false, false, false }; // 本帧刚按下: Left, Right, Middle
        bool mouseDown[3] = { false, false, false }; // Left, Right, Middle
        bool moveForward = false;
        bool moveBackward = false;
        bool moveLeft = false;
        bool moveRight = false;
        bool moveUp = false;
        bool moveDown = false;
    };

    // EditorCamera 只是位姿/投影参数和渲染资源的持有者, 不含任何输入解释逻辑——不同的
    // 控制方式(飞行/环绕/后续可能的轨迹回放等)各自实现一份 ICameraController, 和
    // EditorCamera 自由组合, 而不是把控制逻辑焊死在相机类里。
    class ICameraController
    {
    public:
        virtual ~ICameraController() = default;

        virtual void tick(EditorCamera& camera, const ViewportInput& input, float deltaTime) = 0;
    };
}
