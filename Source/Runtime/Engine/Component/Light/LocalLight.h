#pragma once
#include "Light.h"

namespace hyp
{
    class LocalLight : public Light
    {
        REFLECT_CLASS(LocalLight)
    public:
        LocalLight() = default;

        ~LocalLight() noexcept = default;

        void setAttenuationRadius(float attenuationRadius);

        float getAttenuationRadius() const noexcept
        {
            return mAttenuationRadius;
        }

        void setSourceRadius(float sourceRadius);

        float getSourceRadius() const noexcept
        {
            return mSourceRadius;
        }

    protected:
        float mAttenuationRadius = 5.0f;
        float mSourceRadius = 0.0f;
    };

    template <> Type* Reflection::createType<LocalLight>();
}
