#pragma once
#include "LocalLight.h"
#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    class SpotLight : public LocalLight
    {
        REFLECT_CLASS(SpotLight)
    public:
        SpotLight() = default;

        ~SpotLight() noexcept = default;

        virtual void onRegister() override;

        virtual void onUnregister() override;

        virtual void onTick(float deltaTime) override;

        virtual void onTransformUpdate(const Transform& transform) override;

        void onVisibilityChanged(bool visible) override;

        SpotLightProxyHandle getProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

        void setInnerConeAngle(float innerConeAngle);

        float getInnerConeAngle() const noexcept
        {
            return mInnerConeAngle;
        }

        void setOuterConeAngle(float outerConeAngle);

        float getOuterConeAngle() const noexcept
        {
            return mOuterConeAngle;
        }

    protected:
        void applyLightIntensity() override;

        void applyCastShadows() override;

    private:
        void applyConeAngles();

    private:
        SpotLightProxyHandle mProxyHandle;
        float mInnerConeAngle = 0.0f;
        float mOuterConeAngle = 44.0f;
    };

    template <> Type* Reflection::createType<SpotLight>();
}
