#pragma once
#include "../../Framework/Component.h"
#include "../Collider/Collider.h"

namespace hyp
{
    class Controller : public Component, public ITriggerListener
    {
        REFLECT_CLASS(Controller)
    public:
        Controller();

        ~Controller();

        void onRegister() override;

        void onUnregister() override;

        void onTick(float deltaTime) override;

        void onBeginPlay() override;

        void onEndPlay() override;

        void onTriggerBegin(Collider& self, Collider& other) override;

        void onTriggerEnd(Collider& self, Collider& other) override;

    private:
        float mYaw = 0.0f;
        float mPitch = 0.0f;
    };

    template <> Type* Reflection::createType<Controller>();
}
