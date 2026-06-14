#pragma once
#include <unordered_map>

#include <Core/Common/HString.h>
#include <Core/Math/Vector.h>

namespace hyp
{
    enum class MouseInput
    {
        Button_1,
        Button_2,
        Button_3,
        Button_4,
        Button_5,
        Button_6,
        Button_7,
        Button_8,

        Scroll_X,
        Scroll_Y,
        Move_X,
        Move_Y,

        Button_Left = Button_1,
        Button_Right = Button_2,
        Button_Middle = Button_3,
        Button_Last = Button_8
    };

    enum class GamepadInput
    {
        Button_A,
        Button_B,
        Button_X,
        Button_Y,
        Button_Left_Bumper,
        Button_Right_Bumper,
        Button_Back,
        Button_Start,
        Button_Guide,
        Button_Left_Thumb,
        Button_Right_Thumb,
        Button_DPad_Up,
        Button_DPad_Down,
        Button_DPad_Left,
        Button_DPad_Right,

        Axis_Left_X,
        Axis_Left_Y,
        Axis_Right_X,
        Axis_Right_Y,
        Axis_Left_Trigger,
        Axis_Right_Trigger,

        Button_Last = Button_DPad_Right
    };

    enum class InputDevice : uint8_t
    {
        Keyboard,
        Mouse,
        Gamepad,
    };

    struct InputControl
    {
        InputDevice device;
        uint32_t inputCode;
    };

    enum class TriggerType : uint8_t
    {
        Press,
        Release,
        Hold,
    };

    enum class AxisDirection : uint8_t
    {
        Positive,
        Negative,
    };

    struct ActionModifier
    {
        // 触发 Triggered 状态时的行为
        TriggerType triggerType = TriggerType::Press;
        // 修饰键
        uint8_t modifierMask = 0;
        // Triggered 是否单次触发
        // TODO: implement isOneShot
        bool isOneShot = true;
        // 当 InputControl 为 axis时, 使用 axisDirection 确认轴为什么方向时触发 Triggered
        AxisDirection axisDirection = AxisDirection::Positive;
        // 当 InputControl 为 axis 时, 使用 axisThreshold 确认轴输入超过何值时触发 Triggered
        // 范围: [0.0f, 1.0f], 对于 Positive Direction, value > threshold 触发, 对于 Negative Direction, -value > threshold 触发
        float axisThreshold = 0.5f;
        // 当 triggerType 为 Hold 时, 使用 holdTimeThreshold 确认输入超过多少秒时触发 Triggered
        float holdTimeThreshold = 0.0f;
    };

    struct ActionBinding
    {
        InputControl input;
        ActionModifier modifier;
    };

    struct Axis1DComposite
    {
        InputControl positive;
        InputControl negative;
    };

    using Axis1DSource = std::variant<InputControl, Axis1DComposite>;

    struct Axis1DModifier
    {
        bool invert = false;
        float sensitivity = 1.0f;
        float deadzone = 0.1f;
    };

    struct Axis1DBinding
    {
        Axis1DSource source;
        Axis1DModifier modifier;
    };

    struct Axis2DComposite
    {
        Axis1DSource x;
        Axis1DSource y;
    };

    using Axis2DSource = Axis2DComposite;

    struct Axis2DModifier
    {
        bool invertX = false;
        bool invertY = false;
        bool enableRadialClamp = true;
        float2 sensitivity = float2(1.0f);
        float deadzone = 0.1f;
    };

    struct Axis2DBinding
    {
        Axis2DSource source;
        Axis2DModifier modifier;
    };

    enum class InputActionState
    {
        None,
        Started,
        Ongoing,
        Triggered,
        Ended,
    };

    struct InputAction
    {
        std::vector<ActionBinding> bindings;
        InputActionState state = InputActionState::None;
    };

    struct InputAxis1D
    {
        std::vector<Axis1DBinding> bindings;
        float value = 0.0f;
    };

    struct InputAxis2D
    {
        std::vector<Axis2DBinding> bindings;
        float2 value = float2(0.0f, 0.0f);
    };

    class InputEvent;
    class PlayerInput
    {
    public:
        PlayerInput();

        void preTick();

        bool handle(const InputEvent& event);

        void tick(float deltaTime);

        void addActionBinding(HString actionName, const ActionBinding& binding);

        void addAxis1DBinding(HString axisName, const Axis1DBinding& binding);

        void addAxis2DBinding(HString axisName, const Axis2DBinding& binding);

        InputActionState getActionState(HString actionName) const;

        float getActionHeldDuration(HString actionName) const;

        float getAxis1D(HString axisName) const;

        float2 getAxis2D(HString axisName) const;

    private:
        void accumulateActionDuration(const ActionBinding& binding, float deltaTime) noexcept;

        void applyAxisTriggered(const ActionBinding& binding) noexcept;

        InputActionState getActionState(const ActionBinding& binding) const noexcept;

        float getAxisValue(InputControl input) const noexcept;

        static bool isButton(InputControl input) noexcept;

        static uint32_t getRawAxisValueIndex(InputControl input) noexcept;

    private:
        std::unordered_map<HString, InputAction> mActionBindings;
        std::unordered_map<HString, InputAxis1D> mAxis1DBindings;
        std::unordered_map<HString, InputAxis2D> mAxis2DBindings;

        static constexpr size_t kKeyCount = 512;
        std::array<bool, kKeyCount> mCurrentKeys;
        std::array<bool, kKeyCount> mPreviousKeys;
        std::array<float, kKeyCount> mKeyPressedDurations;

        static constexpr size_t kMouseButtonCount = 8;
        std::array<bool, kMouseButtonCount> mCurrentMouseButtons;
        std::array<bool, kMouseButtonCount> mPreviousMouseButtons;
        std::array<float, kMouseButtonCount> mMouseButtonPressedDurations;

        static constexpr size_t kGamepadButtonCount = 16;
        std::array<bool, kGamepadButtonCount> mCurrentGamepadButtons;
        std::array<bool, kGamepadButtonCount> mPreviousGamepadButtons;
        std::array<float, kGamepadButtonCount> mGamepadButtonPressedDurations;

        enum class RawAxisInputSource
        {
            Mouse_Scroll_X,
            Mouse_Scroll_Y,
            Mouse_Move_X,
            Mouse_Move_Y,
            Gamepad_Axis_Left_X,
            Gamepad_Axis_Left_Y,
            Gamepad_Axis_Right_X,
            Gamepad_Axis_Right_Y,
            Gamepad_Axis_Left_Trigger,
            Gamepad_Axis_Right_Trigger,

            Count,
        };
        static constexpr size_t kRawAxisInputSource = static_cast<size_t>(RawAxisInputSource::Count);
        std::array<float, kRawAxisInputSource> mRawAxisValues;

        std::array<bool, kRawAxisInputSource> mCurrentAxisPositiveTriggered;
        std::array<bool, kRawAxisInputSource> mPreviousAxisPositiveTriggered;
        std::array<float, kRawAxisInputSource> mAxisPositiveTriggeredDurations;

        std::array<bool, kRawAxisInputSource> mCurrentAxisNegativeTriggered;
        std::array<bool, kRawAxisInputSource> mPreviousAxisNegativeTriggered;
        std::array<float, kRawAxisInputSource> mAxisNegativeTriggeredDurations;
    };
}
