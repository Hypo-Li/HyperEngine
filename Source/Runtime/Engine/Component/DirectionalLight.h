#pragma once
#include "Light.h"

#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    class DirectionalLight : public Light
    {
        REFLECT_CLASS(DirectionalLight)
    public:
        DirectionalLight() = default;

        ~DirectionalLight() = default;

        virtual void onRegister() override;

        virtual void onUnregister() override;

        virtual void onTick(float deltaTime) override;

        virtual void onTransformUpdate(const Transform& transform) override;

        void setAffectAtmosphere(bool affectAtmosphere);

        bool isAffectAtmosphere() const noexcept
        {
            return mAffectAtmosphere;
        }

        void setAtmosphereSunLightIndex(uint32_t atmosphereSunLightIndex);

        uint32_t getAtmosphereSunLightIndex() const noexcept
        {
            return mAtmosphereSunLightIndex;
        }

        DirectionalLightProxyHandle getProxyHandle() const noexcept
        {
            return mProxyHandle;
        }

    private:
        uint32_t mCascadeCount = 4;
        uint32_t mCascadeResolution = 1024;
        float mShadowDistance = 400.0f;
        float mDistributionExponent = 3.0f;
        float mTransitionFraction = 0.1f;
        float mDistanceFadeoutFraction = 0.1f;
        float mDepthBias = 0.5f;
        float mSlopeBias = 0.5f;

        bool mAffectAtmosphere = true;
        uint32_t mAtmosphereSunLightIndex = 0;

        DirectionalLightProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<DirectionalLight>();
}
