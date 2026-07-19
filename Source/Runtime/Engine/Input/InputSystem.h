#pragma once
#include "PlayerInput.h"

#include <Core/Math/Vector.h>
#include <Platform/Window.h>

#include <GLFW/glfw3.h>

namespace hyp
{
    enum class InputEventType : uint8_t
    {
        None = 0,

        Key_Press,
        Key_Release,
        Key_Repeat,

        Mouse_Button_Press,
        Mouse_Button_Release,
        Mouse_Scroll,
        Mouse_Move,

        Gamepad_Button_Press,
        Gamepad_Button_Release,
        Gamepad_Axis_Change,
    };

    struct KeyboardData
    {
        uint32_t keyCode;
        uint32_t scancode;
        uint8_t modifiers;
    };

    enum MouseButton : uint8_t
    {
        Button_1,
        Button_2,
        Button_3,
        Button_4,
        Button_5,
        Button_6,
        Button_7,
        Button_8,

        Count,
        Button_Left = Button_1,
        Button_Right = Button_2,
        Button_Middle = Button_3
    };

    struct MouseButtonData
    {
        MouseButton button;
        uint8_t modifiers;
        float xpos;
        float ypos;
    };

    struct MouseMoveData
    {
        float xpos;
        float ypos;
        float deltaX;
        float deltaY;
    };

    struct MouseScrollData
    {
        float offsetX;
        float offsetY;
    };

    struct GamepadButtonData
    {
        uint32_t jid;
        uint32_t button;
    };

    struct GamepadAxisData
    {
        uint32_t jid;
        uint32_t axis;
        float value;
    };

    class InputEvent
    {
    public:
        template <typename T>
        InputEvent(InputEventType type, T data, double timestamp)
        {
            mType = type;
            mData = data;
            mTimestamp = timestamp;
        }

        InputEventType getType() const noexcept
        {
            return mType;
        }

        double getTimestamp() const noexcept
        {
            return mTimestamp;
        }

        template <typename T>
        T getData() const noexcept
        {
            return std::get<T>(mData);
        }

    private:
        InputEventType mType;
        using EventData = std::variant<
            KeyboardData,
            MouseButtonData,
            MouseMoveData,
            MouseScrollData,
            GamepadButtonData,
            GamepadAxisData
        >;
        EventData mData;
        double mTimestamp = 0.0;
    };

    class IInputEventHandler
    {
    public:
        virtual ~IInputEventHandler() = default;

        virtual bool handle(const InputEvent& event) = 0;
    };

    class InputSystem
    {
    public:
        InputSystem(Window& window);

        // 按值接收 unique_ptr: 所有权转移在调用点是显式的(调用方要么 std::make_unique,
        // 要么自己 std::move 一个已有的 unique_ptr), 不会出现"传一个裸指针进来, 它却被悄悄
        // delete 掉"这种和裸指针惯例(裸指针参数默认不持有所有权)相悖的情况。
        void registerEventHandler(std::unique_ptr<IInputEventHandler> eventHandler)
        {
            mEventHandlers.push_back(std::move(eventHandler));
        }

        // 这里的裸指针只是用来按地址查找、比较身份的, 本来就不持有所有权, 用裸指针没问题;
        // 调用方需要在 registerEventHandler() 之前自己留一份原始指针(比如
        // unique_ptr::get()), 才能在之后用它来 unregister。
        void unregisterEventHandler(const IInputEventHandler* eventHandler)
        {
            auto it = std::find_if(mEventHandlers.begin(), mEventHandlers.end(), [eventHandler](const auto& handler)
            {
                return handler.get() == eventHandler;
            });
            if (it != mEventHandlers.end())
                mEventHandlers.erase(it);
        }

        void tick(float deltaTime);

        Window& getWindow() noexcept
        {
            return mWindow;
        }

        PlayerInput& getPlayerInput() noexcept
        {
            return mPlayerInput;
        }

    private:
        void pollGampadInput(uint32_t jid);

    private:
        Window& mWindow;
        std::vector<InputEvent> mEventQueue;
        std::vector<std::unique_ptr<IInputEventHandler>> mEventHandlers;
        PlayerInput mPlayerInput;

        struct GamepadStateSnapshot
        {
            bool isConnected = false;
            uint8_t buttons[15] = { 0 };
            float axes[6] = { 0.0f };
        };
        static constexpr size_t kMaxGamepadCount = 4;
        GamepadStateSnapshot mLastGamepadStates[kMaxGamepadCount];
    };
}
