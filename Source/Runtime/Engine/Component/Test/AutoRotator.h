#pragma once
#include "../../Framework/Component.h"

namespace hyp
{
    class AutoRotator : public Component
    {
        REFLECT_CLASS(AutoRotator)
    public:
        AutoRotator() = default;

        ~AutoRotator() = default;

        virtual void onTick(float deltaTime) override;
    };

    template <> Type* Reflection::createType<AutoRotator>();
}
