#pragma once
#include "Light.h"

#include <RenderBase/ProxyHandle.h>
#include <Renderer/DirectionalLightProxy.h>

namespace hyp
{
    class DirectionalLight : public Light
    {
        REFLECT_CLASS(DirectionalLight)
    public:
        DirectionalLight();

        ~DirectionalLight() = default;

        virtual void onRegister() override;

        virtual void onUnregister() override;

        virtual void onTick(float deltaTime) override;

        virtual void onTransformUpdate(const Transform& transform) override;

        void onVisibilityChanged(bool visible) override;

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

        uint32_t getCascadeCount() const noexcept
        {
            return mCascadeCount;
        }

        uint32_t getCascadeResolution() const noexcept
        {
            return mCascadeResolution;
        }

        float getShadowDistance() const noexcept
        {
            return mShadowDistance;
        }

        float getDistributionExponent() const noexcept
        {
            return mDistributionExponent;
        }

        float getDepthBias() const noexcept
        {
            return mDepthBias;
        }

        float getSlopeBias() const noexcept
        {
            return mSlopeBias;
        }

        float getCascadeBiasDistribution() const noexcept
        {
            return mCascadeBiasDistribution;
        }

        // 跟 DirectionalLightProxy::setShadowParameters() 的分组一致, 一次性设置这 7 个——
        // 这几个字段原来只在 onRegister() 里被读一次传给 proxy 构造函数, 没有任何运行期
        // 更新入口, Inspector 想编辑就必须补上。
        void setShadowParameters(
            uint32_t cascadeCount,
            uint32_t cascadeResolution,
            float shadowDistance,
            float distributionExponent,
            float depthBias,
            float slopeBias,
            float cascadeBiasDistribution
        );

        void setTransitionFraction(float transitionFraction);

        float getTransitionFraction() const noexcept
        {
            return mTransitionFraction;
        }

        void setDistanceFadeoutFraction(float distanceFadeoutFraction);

        float getDistanceFadeoutFraction() const noexcept
        {
            return mDistanceFadeoutFraction;
        }

        void setShadowFilterQuality(ShadowFilterQuality shadowFilterQuality);

        ShadowFilterQuality getShadowFilterQuality() const noexcept
        {
            return mShadowFilterQuality;
        }

    protected:
        void applyLightIntensity() override;

        void applyCastShadows() override;

    private:
        uint32_t mCascadeCount = 4;
        uint32_t mCascadeResolution = 2048;
        float mShadowDistance = 400.0f;
        float mDistributionExponent = 3.0f;
        float mTransitionFraction = 0.1f;
        float mDistanceFadeoutFraction = 0.1f;
        float mDepthBias = 0.5f;
        float mSlopeBias = 0.5f;
        float mCascadeBiasDistribution = 1.0f;
        ShadowFilterQuality mShadowFilterQuality = ShadowFilterQuality::PCF_5x5;

        bool mAffectAtmosphere = true;
        uint32_t mAtmosphereSunLightIndex = 0;

        DirectionalLightProxyHandle mProxyHandle;
    };

    template <> Type* Reflection::createType<DirectionalLight>();
}
