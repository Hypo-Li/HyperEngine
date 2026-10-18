#pragma once
#include "LocalLight.h"
#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    class PointLight : public LocalLight
    {
        REFLECT_CLASS(PointLight)
    public:
        PointLight() = default;

        ~PointLight() noexcept = default;

        virtual void onRegister() override;

        virtual void onUnregister() override;

        virtual void onTick(float deltaTime) override;

        virtual void onTransformUpdate(const Transform& transform) override;

        void onVisibilityChanged(bool visible) override;

        PointLightProxyHandle getProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

    protected:
        void applyLightIntensity() override;

        void applyCastShadows() override;

    private:
        PointLightProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<PointLight>();
}
