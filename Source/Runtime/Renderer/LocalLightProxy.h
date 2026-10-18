#pragma once
#include "LightProxy.h"
#include <Core/Math/Vector.h>

namespace hyp
{
    class LocalLightProxy : public LightProxy
    {
    public:
        struct Desc
        {
            LightProxy::Desc light;
            double3 position;
            float attenuationRadius;
            float sourceRadius;
        };

        LocalLightProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~LocalLightProxy() noexcept = default;

        void setPosition(const double3& position) noexcept;

        const double3& getPosition() const noexcept
        {
            return mPosition;
        }

        void setAttenuationRadius(float attenuationRadius) noexcept;

        float getAttenuationRadius() const noexcept
        {
            return mAttenuationRadius;
        }

        void setSourceRadius(float sourceRadius) noexcept;

        float getSourceRadius() const noexcept
        {
            return mSourceRadius;
        }

    protected:
        // 叶子类在构造里登记进场景之后调用, 包含 initLightData
        void initLocalLightData(LightType type) noexcept;

    private:
        double3 mPosition;
        float mAttenuationRadius;
        float mSourceRadius;
    };
}
