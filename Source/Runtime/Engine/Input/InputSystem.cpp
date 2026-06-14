#include "InputSystem.h"

#include <Core/Common/LogSystem.h>

namespace hyp
{
    InputSystem::InputSystem(Window& window) :
        mWindow(window)
    {
        mWindow.setKeyCallback(
            [this](Window*, int key, int scancode, int action, int mods)
            {
                InputEventType type = action == GLFW_PRESS ?
                    InputEventType::Key_Press : (action == GLFW_RELEASE ?
                    InputEventType::Key_Release : InputEventType::Key_Repeat);
                mEventQueue.emplace_back(
                    type,
                    KeyboardData{ uint32_t(key), uint32_t(scancode), uint8_t(mods)},
                    0.0f
                );
            }
        );
        mWindow.setMouseButtonCallback(
            [this](Window* window, int button, int action, int mods)
            {
                InputEventType type = action == GLFW_PRESS ?
                    InputEventType::Mouse_Button_Press :
                    InputEventType::Mouse_Button_Release;
                double2 cursorPos = window->getCursorPosition();
                mEventQueue.emplace_back(
                    type,
                    MouseButtonData{
                        MouseButton(button),
                        uint8_t(mods),
                        float(cursorPos.x),
                        float(cursorPos.y)
                    },
                    0.0f
                );
            }
        );
        mWindow.setCursorPositionCallback(
            [this](Window* window, double xpos, double ypos)
            {
                static double lastX = xpos, lastY = ypos;
                mEventQueue.emplace_back(
                    InputEventType::Mouse_Move,
                    MouseMoveData{
                        float(xpos),
                        float(ypos),
                        float(xpos - lastX),
                        float(lastY - ypos)
                    },
                    0.0f
                );
                lastX = xpos;
                lastY = ypos;
            }
        );
        mWindow.setScrollCallback(
            [this](Window* window, double offsetX, double offsetY)
            {
                InputEventType type = InputEventType::Mouse_Scroll;
                mEventQueue.emplace_back(
                    InputEventType::Mouse_Scroll,
                    MouseScrollData{
                        float(offsetX),
                        float(offsetY)
                    },
                    0.0f
                );
            }
        );
        Window::setJoystickCallback(
            [this](int jid, int event)
            {
                if (event == GLFW_CONNECTED)
                {
                    if (glfwJoystickIsGamepad(jid))
                    {
                        mLastGamepadStates[jid].isConnected = true;
                    }
                }
                else if (event == GLFW_DISCONNECTED)
                {
                    mLastGamepadStates[jid].isConnected = false;
                }
            }
        );
        for (uint32_t jid = 0; jid < kMaxGamepadCount; ++jid)
        {
            mLastGamepadStates[jid].isConnected = glfwJoystickIsGamepad(jid);
        }
    }

    void InputSystem::tick(float deltaTime)
    {
        mPlayerInput.preTick();
        mWindow.pollEvents();

        for (uint32_t jid = 0; jid < kMaxGamepadCount; ++jid)
        {
            if (mLastGamepadStates[jid].isConnected)
            {
                pollGampadInput(jid);
            }
        }

        for (auto& event : mEventQueue)
        {
            bool handled = false;
            for (auto& eventHandler : mEventHandlers)
            {
                if (eventHandler->handle(event))
                {
                    handled = true;
                    break;
                }
            }

            if (!handled)
            {
                mPlayerInput.handle(event);
            }
        }

        mEventQueue.clear();

        mPlayerInput.tick(deltaTime);
    }

    static constexpr float kAxisDeadzone = 0.1f;
    static constexpr float kAxisChangeThreshold = 0.005f;

    void InputSystem::pollGampadInput(uint32_t jid)
    {
        GLFWgamepadstate currentState;
        if (!glfwGetGamepadState(jid, &currentState))
        {
            return;
        }

        auto& lastState = mLastGamepadStates[jid];
        lastState.isConnected = true;

        for (uint32_t b = 0; b < GLFW_GAMEPAD_BUTTON_LAST; ++b)
        {
            uint8_t currPressed = currentState.buttons[b];
            uint8_t lastPressed = lastState.buttons[b];

            if (currPressed != lastPressed)
            {
                mEventQueue.emplace_back(
                    currPressed ?
                    InputEventType::Gamepad_Button_Press : InputEventType::Gamepad_Button_Release,
                    GamepadButtonData{
                        jid,
                        b
                    },
                    0.0f
                );

                lastState.buttons[b] = currPressed;
            }
        }

        for (uint32_t a = 0; a <= GLFW_GAMEPAD_AXIS_LAST; ++a)
        {
            float currValue = currentState.axes[a];

            if (std::abs(currValue) < kAxisDeadzone)
            {
                currValue = 0.0f;
            }

            float prevValue = lastState.axes[a];

            float delta = std::abs(currValue - prevValue);
            bool isSignificantlyChanged = delta > kAxisChangeThreshold;
            bool isZeroTransition = (currValue == 0.0f && prevValue != 0.0f) || (currValue != 0.0f && prevValue == 0.0f);
            if (isSignificantlyChanged || isZeroTransition)
            {
                mEventQueue.emplace_back(
                    InputEventType::Gamepad_Axis_Change,
                    GamepadAxisData{
                        jid,
                        a,
                        currValue
                    },
                    0.0f
                );

                lastState.axes[a] = currValue;
            }
        }
    }

}
