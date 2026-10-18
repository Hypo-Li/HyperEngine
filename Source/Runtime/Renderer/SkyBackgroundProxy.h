#pragma once
#include "SceneElementProxy.h"

#include <RenderBase/ProxyHandle.h>

namespace hyp
{
    class MaterialProxy;

    class SkyBackgroundProxy : public SceneElementProxy
    {
    public:
        struct Desc
        {
            bool visible = true;
            MaterialProxy* material = nullptr;
            float intensity;
        };

        SkyBackgroundProxy(SceneProxy& sceneProxy, const Desc& desc);

        ~SkyBackgroundProxy() noexcept override;

        void setMaterial(MaterialProxy* material) noexcept
        {
            mMaterial = material;
        }

        MaterialProxy* getMaterial() const noexcept
        {
            return mMaterial;
        }

        void setIntensity(float intensity) noexcept
        {
            mIntensity = intensity;
        }

        float getIntensity() const noexcept
        {
            return mIntensity;
        }

    private:
        MaterialProxy* mMaterial;
        float mIntensity;
    };

    template <>
    struct TProxyTraits<ProxyType::Sky_Background>
    {
        using Proxy = SkyBackgroundProxy;
    };
}
