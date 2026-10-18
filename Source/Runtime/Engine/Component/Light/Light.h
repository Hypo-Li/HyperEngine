#pragma once
#include "../../Framework/Component.h"
#include "../../Framework/Transform.h"

#include <Core/Math/Vector.h>

namespace hyp
{
    class Light : public Component, public ITransformListener
    {
        REFLECT_CLASS(Light)
    public:
        Light() = default;

        virtual ~Light() noexcept = default;

        void setIntensity(float intensity);

        float getIntensity()const noexcept
        {
            return mIntensity;
        }

        void setColor(float3 linearColor);

        float3 getColor() const noexcept
        {
            return mColor;
        }

        void setUseTemperature(bool useTemperature);

        bool isUseTemperature() const noexcept
        {
            return mUseTemperature;
        }

        void setTemperature(float temperature);

        float getTemperature() const noexcept
        {
            return mTemperature;
        }

        void setCastShadows(bool castShadows)
        {
            mCastShadows = castShadows;
            applyCastShadows();
        }

        bool isCastShadows() const noexcept
        {
            return mCastShadows;
        }

    protected:
        virtual void applyLightIntensity() = 0;

        virtual void applyCastShadows() = 0;

        float3 calculateIntensity();

    private:
        float mIntensity = 1.0f;
        float3 mColor = float3(1.0);
        bool mUseTemperature = false;
        float mTemperature = 6500.0f;
        bool mCastShadows = true;
    };

    template <> Type* Reflection::createType<Light>();
}
