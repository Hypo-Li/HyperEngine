#pragma once
#include "InstanceProxy.h"
#include "SceneProxyData.h"

namespace hyp
{
    class LightProxy : public InstanceProxy
    {
    public:
        struct Desc
        {
            InstanceProxy::Desc instance;
            float3 intensity;
            bool castShadows;
        };

        LightProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~LightProxy() noexcept override;

        uint32_t getLightIndex() const noexcept
        {
            return mLightIndex;
        }

        void setIntensity(float3 intensity) noexcept;

        float3 getIntensity() const noexcept
        {
            return mIntensity;
        }

        void setCastShadows(bool castShadows) noexcept;

        bool isCastShadows() const noexcept
        {
            return mCastShadows;
        }

    protected:
        // 叶子类在构造里登记进场景之后调用
        void initLightData(LightType type) noexcept;

    private:
        uint32_t mLightIndex;
        float3 mIntensity;
        bool mCastShadows;
    };
}
