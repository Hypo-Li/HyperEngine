#pragma once
#include "../Framework/Component.h"
#include "../Framework/Transform.h"

#include <Core/Math/Vector.h>

namespace hyp
{
    class Light : public Component, public ITransformListener
    {
        REFLECT_CLASS(Light)
    public:
        Light() = default;

        ~Light() noexcept = default;

        void setIntensity(float intensity)
        {
            mIntensity = intensity;
        }

        float getIntensity()const noexcept
        {
            return mIntensity;
        }

        void setColor(float3 linearColor)
        {
            mColor = linearColor;
        }

        float3 getColor() const noexcept
        {
            return mColor;
        }

        void setUseTemperature(bool useTemperature)
        {
            mUseTemperature = useTemperature;
        }

        bool isUseTemperature() const noexcept
        {
            return mUseTemperature;
        }

        void setTemperature(float temperature)
        {
            mTemperature = temperature;
        }

        float getTemperature() const noexcept
        {
            return mTemperature;
        }

        void setCastShadows(bool castShadows)
        {
            mCastShadows = castShadows;
        }

        bool isCastShadows() const noexcept
        {
            return mCastShadows;
        }

    private:
        float mIntensity = 1.0f;
        float3 mColor = float3(1.0);
        bool mUseTemperature;
        float mTemperature;
        bool mCastShadows = true;
    };

    template <> Type* Reflection::createType<Light>();
}
