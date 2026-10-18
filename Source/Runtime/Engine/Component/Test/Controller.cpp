#include "Controller.h"
#include "../../Framework/Entity.h"
#include "../../Context.h"
#include "../../Input/InputSystem.h"

#include <algorithm>

namespace hyp
{
    REGISTER_CLASS(Controller)

    Controller::Controller()
    {
    }

    Controller::~Controller()
    {
    }

    void Controller::onRegister()
    {
        auto& playerInput = Context::get().getInputSystem().getPlayerInput();
        {
            InputControl axis1DX, axis1DY;
            axis1DX.device = InputDevice::Mouse;
            axis1DX.inputCode = static_cast<uint32_t>(MouseInput::Move_X);
            axis1DY.device = InputDevice::Mouse;
            axis1DY.inputCode = static_cast<uint32_t>(MouseInput::Move_Y);
            Axis2DBinding axisBinding;
            axisBinding.source.x = axis1DX;
            axisBinding.source.y = axis1DY;
            axisBinding.modifier.enableRadialClamp = false;
            axisBinding.modifier.sensitivity = float2(2.0f);
            axisBinding.modifier.deadzone = 0.0f;
            playerInput.addAxis2DBinding("Look"_hs, axisBinding);
        }

        {
            InputControl axis1DX, axis1DY;
            axis1DX.device = InputDevice::Gamepad;
            axis1DX.inputCode = static_cast<uint32_t>(GamepadInput::Axis_Right_X);
            axis1DY.device = InputDevice::Gamepad;
            axis1DY.inputCode = static_cast<uint32_t>(GamepadInput::Axis_Right_Y);
            Axis2DBinding axisBinding;
            axisBinding.source.x = axis1DX;
            axisBinding.source.y = axis1DY;
            axisBinding.modifier.invertY = true;
            axisBinding.modifier.sensitivity = float2(0.2f);
            playerInput.addAxis2DBinding("Look"_hs, axisBinding);
        }

        {
            Axis1DComposite axis1DCompX, axis1DCompY;
            axis1DCompX.positive.device = InputDevice::Keyboard;
            axis1DCompX.positive.inputCode = glfwGetKeyScancode(GLFW_KEY_D);
            axis1DCompX.negative.device = InputDevice::Keyboard;
            axis1DCompX.negative.inputCode = glfwGetKeyScancode(GLFW_KEY_A);
            axis1DCompY.positive.device = InputDevice::Keyboard;
            axis1DCompY.positive.inputCode = glfwGetKeyScancode(GLFW_KEY_W);
            axis1DCompY.negative.device = InputDevice::Keyboard;
            axis1DCompY.negative.inputCode = glfwGetKeyScancode(GLFW_KEY_S);

            Axis2DBinding axisBinding;
            axisBinding.source.x = axis1DCompX;
            axisBinding.source.y = axis1DCompY;

            playerInput.addAxis2DBinding("Move"_hs, axisBinding);

        }

        {
            InputControl axis1DX, axis1DY;
            axis1DX.device = InputDevice::Gamepad;
            axis1DX.inputCode = static_cast<uint32_t>(GamepadInput::Axis_Left_X);
            axis1DY.device = InputDevice::Gamepad;
            axis1DY.inputCode = static_cast<uint32_t>(GamepadInput::Axis_Left_Y);
            Axis2DBinding axisBinding;
            axisBinding.source.x = axis1DX;
            axisBinding.source.y = axis1DY;
            axisBinding.modifier.invertY = true;
            axisBinding.modifier.sensitivity = float2(0.5f);
            playerInput.addAxis2DBinding("Move"_hs, axisBinding);
        }

        {
            ActionBinding actionBinding;
            actionBinding.input.device = InputDevice::Keyboard;
            actionBinding.input.inputCode = glfwGetKeyScancode(GLFW_KEY_SPACE);
            actionBinding.modifier.triggerType = TriggerType::Release;
            actionBinding.modifier.holdTimeThreshold = 2.0f;

            playerInput.addActionBinding("Jump"_hs, actionBinding);
        }

        {
            ActionBinding actionBinding;
            actionBinding.input.device = InputDevice::Gamepad;
            actionBinding.input.inputCode = static_cast<uint32_t>(GamepadInput::Axis_Left_Trigger);
            actionBinding.modifier.axisDirection = AxisDirection::Positive;
            actionBinding.modifier.triggerType = TriggerType::Hold;
            actionBinding.modifier.axisThreshold = 0.5f;

            playerInput.addActionBinding("Up"_hs, actionBinding);
        }

        {
            ActionBinding actionBinding;
            actionBinding.input.device = InputDevice::Keyboard;
            actionBinding.input.inputCode = glfwGetKeyScancode(GLFW_KEY_Q);
            actionBinding.modifier.triggerType = TriggerType::Hold;
            playerInput.addActionBinding("Up"_hs, actionBinding);
        }

        {
            ActionBinding actionBinding;
            actionBinding.input.device = InputDevice::Gamepad;
            actionBinding.input.inputCode = static_cast<uint32_t>(GamepadInput::Axis_Right_Trigger);
            actionBinding.modifier.axisDirection = AxisDirection::Positive;
            actionBinding.modifier.triggerType = TriggerType::Hold;
            actionBinding.modifier.axisThreshold = 0.5f;

            playerInput.addActionBinding("Down"_hs, actionBinding);
        }

        {
            ActionBinding actionBinding;
            actionBinding.input.device = InputDevice::Keyboard;
            actionBinding.input.inputCode = glfwGetKeyScancode(GLFW_KEY_E);
            actionBinding.modifier.triggerType = TriggerType::Hold;
            playerInput.addActionBinding("Down"_hs, actionBinding);
        }

        {
            auto addLookActivationBinding = [&playerInput](MouseButton button)
                {
                    ActionBinding actionBinding;
                    actionBinding.input.device = InputDevice::Mouse;
                    actionBinding.input.inputCode = static_cast<uint32_t>(button);
                    actionBinding.modifier.triggerType = TriggerType::Hold;
                    playerInput.addActionBinding("LookActive"_hs, actionBinding);
                };
            addLookActivationBinding(MouseButton::Button_Left);
            addLookActivationBinding(MouseButton::Button_Right);
        }
    }

    void Controller::onUnregister()
    {
    }

    void Controller::onTick(float deltaTime)
    {
        auto& inputSystem = Context::get().getInputSystem();
        auto& playerInput = inputSystem.getPlayerInput();
        auto& transform = getEntity()->getTransform();

        static constexpr float moveSpeed = 10.0f;

        float2 move = playerInput.getAxis2D("Move"_hs);
        if (move != float2(0))
        {
            transform.setWorldPosition(
                transform.getWorldPosition() +
                double3(transform.getWorldRightVector() * move.x * moveSpeed * deltaTime) +
                double3(transform.getWorldForwardVector() * move.y * moveSpeed * deltaTime)
            );
        }

        auto lookActive = playerInput.getActionState("LookActive"_hs);
        if (lookActive == InputActionState::Triggered)
        {
            // WSLg 不支持鼠标捕获(captureCursor/releaseCursor), 暂时屏蔽
            // inputSystem.getWindow().captureCursor();
            float2 look = playerInput.getAxis2D("Look"_hs);
            if (look != float2(0))
            {
                mYaw -= look.x;
                mPitch += look.y;

                mPitch = std::clamp(mPitch, -89.0f, 89.0f);

                Quaternion qYaw = glm::angleAxis(glm::radians(mYaw), float3{ 0.0f, 0.0f, 1.0f });
                Quaternion qPitch = glm::angleAxis(glm::radians(mPitch), float3{ 1.0f, 0.0f, 0.0f });

                Quaternion newRotation = qYaw * qPitch;
                transform.setWorldRotation(newRotation);
            }
        }
        else if (lookActive == InputActionState::Ended)
        {
            // WSLg 不支持鼠标捕获(captureCursor/releaseCursor), 暂时屏蔽
            inputSystem.getWindow().releaseCursor();
        }

        if (playerInput.getActionState("Up"_hs) == InputActionState::Triggered)
        {
            transform.setWorldPosition(transform.getWorldPosition() + double3(Transform::UpVector * moveSpeed * deltaTime));
        }

        if (playerInput.getActionState("Down"_hs) == InputActionState::Triggered)
        {
            transform.setWorldPosition(transform.getWorldPosition() - double3(Transform::UpVector * moveSpeed * deltaTime));
        }
    }

    void Controller::onBeginPlay()
    {
        if (Collider* collider = getEntity()->getComponent<Collider>())
            collider->addTriggerListener(this);
    }

    void Controller::onEndPlay()
    {
        if (Collider* collider = getEntity()->getComponent<Collider>())
            collider->removeTriggerListener(this);
    }

    void Controller::onTriggerBegin(Collider& self, Collider& other)
    {
        LOG_INFO("Controller::onTriggerBegin: self = {}, other = {}", self.getEntity()->getName().toCString(), other.getEntity()->getName().toCString());
    }

    void Controller::onTriggerEnd(Collider& self, Collider& other)
    {
        LOG_INFO("Controller::onTriggerEnd: self = {}, other = {}", self.getEntity()->getName().toCString(), other.getEntity()->getName().toCString());
    }

    template <> Type* Reflection::createType<Controller>()
    {
        auto c = new TClass<Controller, Component>("Controller");
        return c;
    }
}
