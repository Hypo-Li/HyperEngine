#pragma once
#include "LocalLightProxy.h"

namespace hyp
{
    class SpotLightProxy : public LocalLightProxy
    {
    public:
        struct Desc
        {
            LocalLightProxy::Desc local;
            float innerConeAngle;
            float outerConeAngle;
        };

        SpotLightProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~SpotLightProxy() noexcept override;

        void setConeAngles(float innerConeAngle, float outerConeAngle) noexcept;

        float getInnerConeAngle() const noexcept
        {
            return mInnerConeAngle;
        }

        float getOuterConeAngle() const noexcept
        {
            return mOuterConeAngle;
        }

    private:
        void applyConeAngles() noexcept;

    private:
        float mInnerConeAngle;
        float mOuterConeAngle;
    };

    template <>
    struct TProxyTraits<ProxyType::Spot_Light>
    {
        using Proxy = SpotLightProxy;
    };
}
