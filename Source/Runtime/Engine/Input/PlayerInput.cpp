#include "PlayerInput.h"
#include "InputSystem.h"
#include <Core/Common/LogSystem.h>
#include <Core/Math/Math.h>

#include <algorithm>

namespace hyp
{
    PlayerInput::PlayerInput()
    {
        std::fill(mCurrentKeys.begin(), mCurrentKeys.end(), false);
        std::fill(mKeyPressedDurations.begin(), mKeyPressedDurations.end(), 0.0f);
        std::fill(mCurrentMouseButtons.begin(), mCurrentMouseButtons.end(), false);
        std::fill(mMouseButtonPressedDurations.begin(), mMouseButtonPressedDurations.end(), 0.0f);
        std::fill(mCurrentGamepadButtons.begin(), mCurrentGamepadButtons.end(), false);
        std::fill(mGamepadButtonPressedDurations.begin(), mGamepadButtonPressedDurations.end(), 0.0f);
        std::fill(mRawAxisValues.begin(), mRawAxisValues.end(), 0.0f);
        std::fill(mCurrentAxisPositiveTriggered.begin(), mCurrentAxisPositiveTriggered.end(), false);
        std::fill(mAxisPositiveTriggeredDurations.begin(), mAxisPositiveTriggeredDurations.end(), 0.0f);
        std::fill(mCurrentAxisNegativeTriggered.begin(), mCurrentAxisNegativeTriggered.end(), false);
        std::fill(mAxisNegativeTriggeredDurations.begin(), mAxisNegativeTriggeredDurations.end(), 0.0f);
    }

    void PlayerInput::preTick()
    {
        std::copy(mCurrentKeys.begin(), mCurrentKeys.end(), mPreviousKeys.begin());
        std::copy(mCurrentMouseButtons.begin(), mCurrentMouseButtons.end(), mPreviousMouseButtons.begin());
        std::copy(mCurrentGamepadButtons.begin(), mCurrentGamepadButtons.end(), mPreviousGamepadButtons.begin());
        std::copy(mCurrentAxisPositiveTriggered.begin(), mCurrentAxisPositiveTriggered.end(), mPreviousAxisPositiveTriggered.begin());
        std::copy(mCurrentAxisNegativeTriggered.begin(), mCurrentAxisNegativeTriggered.end(), mPreviousAxisNegativeTriggered.begin());
        mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Scroll_X)] = 0.0f;
        mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Scroll_Y)] = 0.0f;
        mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Move_X)] = 0.0f;
        mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Move_Y)] = 0.0f;
    }

    bool PlayerInput::handle(const InputEvent& event)
    {
        switch (event.getType())
        {
        case InputEventType::Key_Press:
        case InputEventType::Key_Release:
        {
            auto eventData = event.getData<KeyboardData>();
            mCurrentKeys[eventData.scancode] = !(event.getType() == InputEventType::Key_Release);
            return true;
        }
        case InputEventType::Mouse_Button_Press:
        case InputEventType::Mouse_Button_Release:
        {
            auto eventData = event.getData<MouseButtonData>();
            mCurrentMouseButtons[static_cast<uint8_t>(eventData.button)] = !(event.getType() == InputEventType::Mouse_Button_Release);
            return true;
        }
        case InputEventType::Mouse_Scroll:
        {
            auto eventData = event.getData<MouseScrollData>();
            mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Scroll_X)] = eventData.offsetX;
            mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Scroll_Y)] = eventData.offsetY;
            return true;
        }
        case InputEventType::Mouse_Move:
        {
            auto eventData = event.getData<MouseMoveData>();
            mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Move_X)] = eventData.deltaX;
            mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Move_Y)] = eventData.deltaY;
            return true;
        }
        case InputEventType::Gamepad_Button_Press:
        case InputEventType::Gamepad_Button_Release:
        {
            auto eventData = event.getData<GamepadButtonData>();
            mCurrentGamepadButtons[static_cast<uint8_t>(eventData.button)] = !(event.getType() == InputEventType::Gamepad_Button_Release);
            return true;
        }
        case InputEventType::Gamepad_Axis_Change:
        {
            // TODO: 支持多个手柄
            auto eventData = event.getData<GamepadAxisData>();
            RawAxisInputSource axis;
            switch (eventData.axis)
            {
            case GLFW_GAMEPAD_AXIS_LEFT_X: axis = RawAxisInputSource::Gamepad_Axis_Left_X; break;
            case GLFW_GAMEPAD_AXIS_LEFT_Y: axis = RawAxisInputSource::Gamepad_Axis_Left_Y; break;
            case GLFW_GAMEPAD_AXIS_RIGHT_X: axis = RawAxisInputSource::Gamepad_Axis_Right_X; break;
            case GLFW_GAMEPAD_AXIS_RIGHT_Y: axis = RawAxisInputSource::Gamepad_Axis_Right_Y; break;
            case GLFW_GAMEPAD_AXIS_LEFT_TRIGGER: axis = RawAxisInputSource::Gamepad_Axis_Left_Trigger; break;
            case GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER: axis = RawAxisInputSource::Gamepad_Axis_Right_Trigger; break;
            }
            mRawAxisValues[static_cast<size_t>(axis)] = eventData.value;
            return true;
        }
        default:
            return false;
        }
    }

    int getInputActionStatePriority(InputActionState state)
    {
        switch (state)
        {
        case InputActionState::Started: return 2;
        case InputActionState::Ongoing: return 1;
        case InputActionState::Triggered: return 3;
        default: return 0;
        }
    }

    void PlayerInput::tick(float deltaTime)
    {
        for (auto& [name, action] : mActionBindings)
        {
            InputActionState highestState = InputActionState::None;
            for (const auto& binding : action.bindings)
            {
                accumulateActionDuration(binding, deltaTime);
                if (!isButton(binding.input))
                {
                    applyAxisTriggered(binding);
                }
                InputActionState state = getActionState(binding);
                if (getInputActionStatePriority(state) > getInputActionStatePriority(highestState))
                {
                    highestState = state;
                }
            }
            action.state = highestState;
        }

        auto evaluateAxis1DSource = [this](const Axis1DSource& source) -> float
            {
                float contribution = 0.0f;
                if (std::holds_alternative<InputControl>(source))
                {
                    InputControl inputControl = std::get<InputControl>(source);
                    contribution = getAxisValue(inputControl);
                }
                else
                {
                    Axis1DComposite axis1DComposite = std::get<Axis1DComposite>(source);
                    contribution += getAxisValue(axis1DComposite.positive);
                    contribution -= getAxisValue(axis1DComposite.negative);
                }
                return contribution;
            };

        for (auto& [name, axis1D] : mAxis1DBindings)
        {
            axis1D.value = 0.0f;
            float maxPositive = 0.0f;
            float maxNegative = 0.0f;
            for (const auto& binding : axis1D.bindings)
            {
                float delta = evaluateAxis1DSource(binding.source);
                if (std::abs(delta) > binding.modifier.deadzone)
                {
                    float factor = binding.modifier.invert ? -binding.modifier.sensitivity : binding.modifier.sensitivity;

                    delta *= factor;

                    if (delta > 0.0f)
                        maxPositive = std::max(maxPositive, delta);
                    else
                        maxNegative = std::min(maxNegative, delta);
                }
            }

            axis1D.value = maxPositive + maxNegative;
        }

        for (auto& [name, axis2D] : mAxis2DBindings)
        {
            axis2D.value = float2(0.0f);
            float2 highestContribution = float2(0.0f);
            float maxLenSqr = 0.0f;
            for (const auto& binding : axis2D.bindings)
            {
                float2 contribution = float2{
                    evaluateAxis1DSource(binding.source.x),
                    evaluateAxis1DSource(binding.source.y)
                };

                float rawLenSqr = 0.0f;
                if (binding.modifier.enableRadialClamp)
                {
                    float rawLength = Math::length(contribution);

                    float minDead = binding.modifier.deadzone;
                    float maxDead = 1.0f;

                    if (rawLength > minDead)
                    {
                        float clampedLength = std::min(rawLength, maxDead);
                        float normalizedLength = (clampedLength - minDead) / (maxDead - minDead);
                        contribution = (contribution / rawLength) * normalizedLength;
                        rawLenSqr = Math::lengthSqr(contribution);
                    }
                    else
                    {
                        contribution = float2(0.0f);
                    }
                }
                else
                {
                    float deadzone = binding.modifier.deadzone;
                    if (std::abs(contribution.x) < deadzone) contribution.x = 0.0f;
                    if (std::abs(contribution.y) < deadzone) contribution.y = 0.0f;

                    rawLenSqr = Math::lengthSqr(contribution);
                }

                if (rawLenSqr > 0.0f)
                {
                    if (binding.modifier.invertX) contribution.x *= -1.0f;
                    if (binding.modifier.invertY) contribution.y *= -1.0f;

                    contribution.x *= binding.modifier.sensitivity.x;
                    contribution.y *= binding.modifier.sensitivity.y;

                    if (rawLenSqr > maxLenSqr)
                    {
                        maxLenSqr = rawLenSqr;
                        highestContribution = contribution;
                    }
                }

                axis2D.value = highestContribution;
            }
        }
    }

    void PlayerInput::addActionBinding(HString actionName, const ActionBinding& binding)
    {
        mActionBindings[actionName].bindings.push_back(binding);
    }

    void PlayerInput::addAxis1DBinding(HString axisName, const Axis1DBinding& binding)
    {
        mAxis1DBindings[axisName].bindings.push_back(binding);
    }

    void PlayerInput::addAxis2DBinding(HString axisName, const Axis2DBinding& binding)
    {
        mAxis2DBindings[axisName].bindings.push_back(binding);
    }

    InputActionState PlayerInput::getActionState(HString actionName) const
    {
        return mActionBindings.at(actionName).state;
    }

    float PlayerInput::getActionHeldDuration(HString actionName) const
    {
        auto& bindings = mActionBindings.at(actionName).bindings;
        float maxDuration = 0.0f;
        for (auto& binding : bindings)
        {
            float duration = 0.0f;
            if (isButton(binding.input))
            {
                switch (binding.input.device)
                {
                case InputDevice::Keyboard:
                {
                    duration = mKeyPressedDurations[binding.input.inputCode];
                    break;
                }
                case InputDevice::Mouse:
                {
                    duration = mMouseButtonPressedDurations[binding.input.inputCode];
                    break;
                }
                case InputDevice::Gamepad:
                {
                    duration = mGamepadButtonPressedDurations[binding.input.inputCode];
                    break;
                }
                }
            }
            else
            {
                uint32_t rawAxisValueIndex = getRawAxisValueIndex(binding.input);
                if (binding.modifier.axisDirection == AxisDirection::Positive)
                {
                    duration = mAxisPositiveTriggeredDurations[rawAxisValueIndex];
                }
                else
                {
                    duration = mAxisNegativeTriggeredDurations[rawAxisValueIndex];
                }
            }
            maxDuration = std::max(maxDuration, duration);
        }
        return maxDuration;
    }

    float PlayerInput::getAxis1D(HString axisName) const
    {
        return mAxis1DBindings.at(axisName).value;
    }

    float2 PlayerInput::getAxis2D(HString axisName) const
    {
        return mAxis2DBindings.at(axisName).value;
    }

    void accmulateActionDuration(bool prev, bool curr, float& duration, float deltaTime) noexcept
    {
        if (curr)
        {
            if (prev)
                duration += deltaTime;
            else
                duration = 0.0f;
        }
        else
        {
            if (prev)
                duration += deltaTime;
            else
                duration = 0.0f;
        }
    }

    void PlayerInput::accumulateActionDuration(const ActionBinding& binding, float deltaTime) noexcept
    {
        auto input = binding.input;
        if (!isButton(input))
        {
            uint32_t rawAxisValueIndex = getRawAxisValueIndex(input);
            if (binding.modifier.axisDirection == AxisDirection::Positive)
            {
                accmulateActionDuration(
                    mPreviousAxisPositiveTriggered[rawAxisValueIndex],
                    mCurrentAxisPositiveTriggered[rawAxisValueIndex],
                    mAxisPositiveTriggeredDurations[rawAxisValueIndex],
                    deltaTime
                );
            }
            else
            {
                accmulateActionDuration(
                    mPreviousAxisNegativeTriggered[rawAxisValueIndex],
                    mCurrentAxisNegativeTriggered[rawAxisValueIndex],
                    mAxisNegativeTriggeredDurations[rawAxisValueIndex],
                    deltaTime
                );
            }

            return;
        }

        switch (input.device)
        {
        case InputDevice::Keyboard:
        {
            accmulateActionDuration(
                mPreviousKeys[input.inputCode],
                mCurrentKeys[input.inputCode],
                mKeyPressedDurations[input.inputCode],
                deltaTime
            );
            break;
        }
        case InputDevice::Mouse:
        {
            accmulateActionDuration(
                mPreviousMouseButtons[input.inputCode],
                mCurrentMouseButtons[input.inputCode],
                mMouseButtonPressedDurations[input.inputCode],
                deltaTime
            );
            break;
        }
        case InputDevice::Gamepad:
        {
            accmulateActionDuration(
                mPreviousGamepadButtons[input.inputCode],
                mCurrentGamepadButtons[input.inputCode],
                mGamepadButtonPressedDurations[input.inputCode],
                deltaTime
            );
            break;
        }
        }
    }

    void PlayerInput::applyAxisTriggered(const ActionBinding& binding) noexcept
    {
        auto modifier = binding.modifier;
        uint32_t rawAxisValueIndex = getRawAxisValueIndex(binding.input);

        if (modifier.axisDirection == AxisDirection::Positive)
        {
            mCurrentAxisPositiveTriggered[rawAxisValueIndex] = mRawAxisValues[rawAxisValueIndex] > modifier.axisThreshold;
        }
        else
        {
            mCurrentAxisNegativeTriggered[rawAxisValueIndex] = -mRawAxisValues[rawAxisValueIndex] > modifier.axisThreshold;
        }
    }

    // FIXME: 还需要实现mods
    InputActionState PlayerInput::getActionState(const ActionBinding& binding) const noexcept
    {
        InputDevice inputDevice = binding.input.device;
        uint32_t inputCode = binding.input.inputCode;
        auto modifier = binding.modifier;

        bool currentPressed = false;
        bool previousPressed = false;
        bool holdEnough = false;

        if (isButton(binding.input))
        {
            switch (inputDevice)
            {
            case InputDevice::Keyboard:
            {
                currentPressed = mCurrentKeys[inputCode];
                previousPressed = mPreviousKeys[inputCode];
                holdEnough = mKeyPressedDurations[inputCode] >= modifier.holdTimeThreshold;
                break;
            }
            case InputDevice::Mouse:
            {
                currentPressed = mCurrentMouseButtons[inputCode];
                previousPressed = mPreviousMouseButtons[inputCode];
                holdEnough = mMouseButtonPressedDurations[inputCode] >= modifier.holdTimeThreshold;
                break;
            }
            case InputDevice::Gamepad:
            {
                currentPressed = mCurrentGamepadButtons[inputCode];
                previousPressed = mPreviousGamepadButtons[inputCode];
                holdEnough = mGamepadButtonPressedDurations[inputCode] >= modifier.holdTimeThreshold;
                break;
            }
            }
        }
        else
        {
            uint32_t rawAxisValueIndex = getRawAxisValueIndex(binding.input);
            if (modifier.axisDirection == AxisDirection::Positive)
            {
                currentPressed = mCurrentAxisPositiveTriggered[rawAxisValueIndex];
                previousPressed = mPreviousAxisPositiveTriggered[rawAxisValueIndex];
                holdEnough = mAxisPositiveTriggeredDurations[rawAxisValueIndex] >= modifier.holdTimeThreshold;
            }
            else
            {
                currentPressed = mCurrentAxisNegativeTriggered[rawAxisValueIndex];
                previousPressed = mPreviousAxisNegativeTriggered[rawAxisValueIndex];
                holdEnough = mAxisNegativeTriggeredDurations[rawAxisValueIndex] >= modifier.holdTimeThreshold;
            }
        }

        InputActionState state = InputActionState::None;
        if (currentPressed)
        {
            if (previousPressed)
            {
                state = (modifier.triggerType == TriggerType::Hold && holdEnough) ?
                    InputActionState::Triggered : InputActionState::Ongoing;
            }
            else
            {
                state = modifier.triggerType == TriggerType::Press ?
                    InputActionState::Triggered : InputActionState::Started;
            }
        }
        else
        {
            if (previousPressed)
            {
                state = modifier.triggerType == TriggerType::Release ?
                    InputActionState::Triggered : InputActionState::Ended;
            }
            else
            {

                state = InputActionState::None;
            }
        }
        return state;
    }

    static constexpr float kDegreePerPixel = 0.022f;

    float PlayerInput::getAxisValue(InputControl input) const noexcept
    {
        switch (input.device)
        {
        case InputDevice::Keyboard:
        {
            return mCurrentKeys[input.inputCode] ? 1.0f : 0.0f;
        }
        case InputDevice::Mouse:
        {
            switch (input.inputCode)
            {
            case static_cast<uint32_t>(MouseInput::Scroll_X):
            {
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Scroll_X)];
            }
            case static_cast<uint32_t>(MouseInput::Scroll_Y):
            {
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Scroll_Y)];
            }
            case static_cast<uint32_t>(MouseInput::Move_X):
            {
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Move_X)] * kDegreePerPixel;
            }
            case static_cast<uint32_t>(MouseInput::Move_Y):
            {
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Mouse_Move_Y)] * kDegreePerPixel;
            }
            default:
                return mCurrentMouseButtons[input.inputCode] ? 1.0f : 0.0f;
            }
        }
        case InputDevice::Gamepad:
        {
            switch (input.inputCode)
            {
            case static_cast<uint32_t>(GamepadInput::Axis_Left_X):
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Gamepad_Axis_Left_X)];
            case static_cast<uint32_t>(GamepadInput::Axis_Left_Y):
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Gamepad_Axis_Left_Y)];
            case static_cast<uint32_t>(GamepadInput::Axis_Right_X):
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Gamepad_Axis_Right_X)];
            case static_cast<uint32_t>(GamepadInput::Axis_Right_Y):
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Gamepad_Axis_Right_Y)];
            case static_cast<uint32_t>(GamepadInput::Axis_Left_Trigger):
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Gamepad_Axis_Left_Trigger)];
            case static_cast<uint32_t>(GamepadInput::Axis_Right_Trigger):
                return mRawAxisValues[static_cast<size_t>(RawAxisInputSource::Gamepad_Axis_Right_Trigger)];
            default:
                return mCurrentGamepadButtons[input.inputCode] ? 1.0f : 0.0f;
            }
        }
        default:
            return 0.0f;
        }
    }

    bool PlayerInput::isButton(InputControl input) noexcept
    {
        switch (input.device)
        {
        case InputDevice::Keyboard:
        {
            return true;
        }
        case InputDevice::Mouse:
        {
            return input.inputCode <= static_cast<uint32_t>(MouseInput::Button_Last);
        }
        case InputDevice::Gamepad:
        {
            return input.inputCode <= static_cast<uint32_t>(GamepadInput::Button_Last);
        }
        default:
            return false;
        }
    }

    uint32_t PlayerInput::getRawAxisValueIndex(InputControl input) noexcept
    {
        uint32_t result = ~0u;
        switch (input.device)
        {
        case InputDevice::Mouse:
        {
            switch (input.inputCode)
            {
            case static_cast<uint32_t>(MouseInput::Scroll_X):
                result = static_cast<uint32_t>(RawAxisInputSource::Mouse_Scroll_X); break;
            case static_cast<uint32_t>(MouseInput::Scroll_Y):
                result = static_cast<uint32_t>(RawAxisInputSource::Mouse_Scroll_Y); break;
            case static_cast<uint32_t>(MouseInput::Move_X):
                result = static_cast<uint32_t>(RawAxisInputSource::Mouse_Move_X); break;
            case static_cast<uint32_t>(MouseInput::Move_Y):
                result = static_cast<uint32_t>(RawAxisInputSource::Mouse_Move_Y); break;
            }
            break;
        }
        case InputDevice::Gamepad:
        {
            switch (input.inputCode)
            {
            case static_cast<uint32_t>(GamepadInput::Axis_Left_X):
                result = static_cast<uint32_t>(RawAxisInputSource::Gamepad_Axis_Left_X); break;
            case static_cast<uint32_t>(GamepadInput::Axis_Left_Y):
                result = static_cast<uint32_t>(RawAxisInputSource::Gamepad_Axis_Left_Y); break;
            case static_cast<uint32_t>(GamepadInput::Axis_Right_X):
                result = static_cast<uint32_t>(RawAxisInputSource::Gamepad_Axis_Right_X); break;
            case static_cast<uint32_t>(GamepadInput::Axis_Right_Y):
                result = static_cast<uint32_t>(RawAxisInputSource::Gamepad_Axis_Right_Y); break;
            case static_cast<uint32_t>(GamepadInput::Axis_Left_Trigger):
                result = static_cast<uint32_t>(RawAxisInputSource::Gamepad_Axis_Left_Trigger); break;
            case static_cast<uint32_t>(GamepadInput::Axis_Right_Trigger):
                result = static_cast<uint32_t>(RawAxisInputSource::Gamepad_Axis_Right_Trigger); break;
            }
            break;
        }
        }
        return result;
    }



}
