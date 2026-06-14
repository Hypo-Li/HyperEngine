#pragma once
#include "../Framework/Component.h"

namespace hyp
{
    class Controller : public Component
    {
        REFLECT_CLASS(Controller)
    public:
        Controller();

        ~Controller();

        virtual void onRegister() override;

        virtual void onUnregister() override;

        virtual void onTick(float deltaTime) override;

    private:
        float mYaw = 0.0f;
        float mPitch = 0.0f;
    };

    template <> Type* Reflection::createType<Controller>();
}
