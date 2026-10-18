#include "SpotLightProxy.h"
#include "SceneProxy.h"

#include <algorithm>
#include <cmath>

namespace hyp
{
    namespace
    {
        constexpr float kPi = 3.14159265358979323846f;
        // 跟 UE USpotLightComponent::GetClampedConeAngles() 同一套 clamp: 角度上限 88.9 度避免
        // 退化成平面, outer 至少比 inner 大一点(弧度制 0.001)避免 invCosConeDifference 除零
        constexpr float kMaxConeAngleDegrees = 88.9f;
        constexpr float kConeAngleEpsilonRad = 0.001f;

        void computeClampedConeAngles(float innerConeAngle, float outerConeAngle, float& outInnerRad, float& outOuterRad) noexcept
        {
            float maxRad = kMaxConeAngleDegrees * kPi / 180.0f;
            outInnerRad = std::clamp(innerConeAngle, 0.0f, kMaxConeAngleDegrees) * kPi / 180.0f;
            outOuterRad = std::clamp(outerConeAngle * kPi / 180.0f, outInnerRad + kConeAngleEpsilonRad, maxRad + kConeAngleEpsilonRad);
        }
    }

    SpotLightProxy::SpotLightProxy(SceneProxy& sceneProxy, const Desc& desc) :
        LocalLightProxy(sceneProxy, desc.local),
        mInnerConeAngle(desc.innerConeAngle),
        mOuterConeAngle(desc.outerConeAngle)
    {
        sceneProxy.registerElement(this);
        initLocalLightData(LightType::Spot);
        applyConeAngles();
    }

    SpotLightProxy::~SpotLightProxy() noexcept
    {
        getSceneProxy().unregisterElement(this);
    }

    void SpotLightProxy::setConeAngles(float innerConeAngle, float outerConeAngle) noexcept
    {
        mInnerConeAngle = innerConeAngle;
        mOuterConeAngle = outerConeAngle;
        applyConeAngles();
    }

    void SpotLightProxy::applyConeAngles() noexcept
    {
        float innerRad, outerRad;
        computeClampedConeAngles(mInnerConeAngle, mOuterConeAngle, innerRad, outerRad);
        float cosInnerCone = std::cos(innerRad);
        float cosOuterCone = std::cos(outerRad);

        SceneProxy& sceneProxy = getSceneProxy();
        auto& lightData = sceneProxy.getLightData(getLightIndex());
        lightData.cosOuterCone = cosOuterCone;
        lightData.invCosConeDifference = 1.0f / (cosInnerCone - cosOuterCone);
        sceneProxy.markLightDataUpdated(getLightIndex());
    }
}
